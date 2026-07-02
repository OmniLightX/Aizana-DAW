#include "MixerView.h"

//==============================================================================

MixerView::MixerView (ApplicationState& appState_, MixerEngine& mixerEngine_,
                      PluginManager& pluginManager_, PluginWindowManager& pluginWindowManager_,
                      PluginScanner& pluginScanner_)
    : appState (appState_)
    , mixerEngine (mixerEngine_)
    , pluginManager (pluginManager_)
    , pluginWindowManager (pluginWindowManager_)
    , pluginScanner (pluginScanner_)
{
    channelStripViewport.setScrollBarsShown (false, true);  // horizontal scroll only
    channelStripViewport.setViewedComponent (&channelStripContainer, false);
    addAndMakeVisible (channelStripViewport);

    channelStripContainer.onRightClick = [this] (const juce::MouseEvent& e) { showContextMenu (e); };

    // Visible Add-Channel entry point — same menu as the background right-click
    addChannelButton.setTooltip ("Bus-, FX- oder VCA-Channel hinzuf\xc3\xbcgen");
    addChannelButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2e2e2e));
    addChannelButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    addChannelButton.onClick = [this] { showAddChannelMenu (&addChannelButton); };
    addAndMakeVisible (addChannelButton);

    // Register ValueTree listener so we rebuild on track add/remove
    if (auto* edit = appState.editManager.getEdit())
        edit->state.addListener (this);

    rebuildStrips();

    startTimerHz (24);

    setWantsKeyboardFocus (true);
}

MixerView::~MixerView()
{
    stopTimer();  // Stop before destroying strips to avoid dangling timerCallback

    if (auto* edit = appState.editManager.getEdit())
        edit->state.removeListener (this);

    detachInputLevelClients();

    // Unregister level clients from TE's LevelMeterPlugin before destruction.
    for (auto& [track, client] : levelClients)
        if (auto* lp = track->getLevelMeterPlugin())
            lp->measurer.removeClient (*client);
    levelClients.clear();

    // Clear strips (they reference MeterProcessors); then clear MeterProcessors
    strips.clear();
    busFXStrips.clear();
    vcaStrips.clear();
    masterStrip.reset();
    meterProcessors.clear();
}

void MixerView::resized()
{
    auto area = getLocalBounds();

    // Master strip is pinned to the right edge of MixerView (outside the scrollable
    // viewport) so it stays visible regardless of how many tracks exist.
    if (masterStrip != nullptr)
        masterStrip->setBounds (area.removeFromRight (kMasterStripWidth));

    // Narrow fixed column on the left for the Add-Channel button
    auto addColumn = area.removeFromLeft (kAddColumnWidth);
    addChannelButton.setBounds (addColumn.removeFromTop (kAddColumnWidth).reduced (3));

    channelStripViewport.setBounds (area);

    // Compute candidate strip height from current geometry.
    const int viewH = area.getHeight();

    // Only update cached height if we have a meaningful parent-assigned height.
    if (viewH > 100)
    {
        const int scrollBarH = channelStripViewport.getScrollBarThickness();
        cachedStripHeight = juce::jmax (200, viewH - scrollBarH);
    }

    const int stripH = cachedStripHeight;

    // Total strip count: audio + bus/fx + vca channels
    const int totalStrips = strips.size() + busFXStrips.size() + vcaStrips.size();
    const int totalW = juce::jmax (totalStrips * kStripWidth,
                                   channelStripViewport.getWidth());
    channelStripContainer.setSize (totalW, stripH);

    // Layout: audio strips first, then bus/fx strips, then VCA strips
    int xPos = 0;
    for (int i = 0; i < strips.size(); ++i, xPos += kStripWidth)
        strips[i]->setBounds (xPos, 0, kStripWidth, stripH);

    for (int i = 0; i < busFXStrips.size(); ++i, xPos += kStripWidth)
        busFXStrips[i]->setBounds (xPos, 0, kStripWidth, stripH);

    for (int i = 0; i < vcaStrips.size(); ++i, xPos += kStripWidth)
        vcaStrips[i]->setBounds (xPos, 0, kStripWidth, stripH);
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));

    // Top border separating mixer from arrangement
    g.setColour (juce::Colour (0xff000000));
    g.drawLine (0.0f, 0.0f, (float)getWidth(), 0.0f, 2.0f);

    // Vertical divider between scrollable strips and the pinned master strip
    if (masterStrip != nullptr)
    {
        const float divX = (float)(getWidth() - kMasterStripWidth);
        g.setColour (juce::Colour (0xff000000));
        g.drawLine (divX, 0.0f, divX, (float)getHeight(), 2.0f);
    }
}

void MixerView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showContextMenu (e);
}

void MixerView::showContextMenu (const juce::MouseEvent& /*e*/)
{
    showAddChannelMenu (this);
}

void MixerView::showAddChannelMenu (juce::Component* target)
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Add Channel");
    menu.addItem (1, "Add Bus Channel");
    menu.addItem (2, "Add FX Channel");
    menu.addItem (3, "Add VCA Channel");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
        [this] (int result)
        {
            if (result == 1)
            {
                mixerEngine.createBusChannel ("Bus " + juce::String (mixerEngine.getBusChannels().size() + 1));
            }
            else if (result == 2)
            {
                mixerEngine.createFXChannel ("FX " + juce::String (mixerEngine.getFXChannels().size() + 1));
            }
            else if (result == 3)
            {
                mixerEngine.createVCAChannel ("VCA " + juce::String (mixerEngine.getVCAChannels().size() + 1));
            }
        });
}

MeterProcessor* MixerView::getMeterProcessorForTrack (const te::AudioTrack& track)
{
    auto it = meterProcessors.find (const_cast<te::AudioTrack*> (&track));
    return (it != meterProcessors.end()) ? it->second.get() : nullptr;
}

void MixerView::clearStrips()
{
    // Clear selection — strip pointers are about to be destroyed.
    // All selection arrays must be cleared before strips.clear() to prevent dangling access.
    selectedMixerTracks.clear();
    selectedStrips.clear();
    selectedBusFXStrips.clear();

    // Unregister level clients before clearing — the referenced tracks must
    // still be alive here (on an Edit swap this runs from detachFromEdit()
    // BEFORE the old Edit is destroyed).
    for (auto& [track, client] : levelClients)
        if (auto* lp = track->getLevelMeterPlugin())
            lp->measurer.removeClient (*client);
    levelClients.clear();
    detachInputLevelClients();

    channelStripContainer.removeAllChildren();
    strips.clear();
    busFXStrips.clear();
    vcaStrips.clear();
    masterStrip.reset();
    meterProcessors.clear();
}

void MixerView::detachFromEdit()
{
    // Called BEFORE the old Edit is destroyed (File>New/Open): drop every
    // reference into it — the state listener and all track-bound strips.
    if (auto* edit = appState.editManager.getEdit())
        edit->state.removeListener (this);

    clearStrips();
}

void MixerView::attachToEdit()
{
    // Called after the new Edit is fully constructed.
    if (auto* edit = appState.editManager.getEdit())
        edit->state.addListener (this);

    rebuildStrips();
}

void MixerView::rebuildStrips()
{
    clearStrips();

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    // ---- Audio track strips (plain audio channels only) ----
    for (auto* audioTrack : te::getAudioTracks (*edit))
    {
        if (! MixerEngine::isAudioChannel (*audioTrack))
            continue;   // Bus/FX channels get their own strip below

        auto meter = std::make_unique<MeterProcessor>();
        meter->prepareToPlay (44100.0, 512);
        auto* meterPtr = meter.get();
        meterProcessors[audioTrack] = std::move (meter);

        auto client = std::make_unique<te::LevelMeasurer::Client>();
        if (auto* levelPlugin = audioTrack->getLevelMeterPlugin())
            levelPlugin->measurer.addClient (*client);
        levelClients[audioTrack] = std::move (client);

        auto* strip = strips.add (new ChannelStrip (*audioTrack, mixerEngine, *meterPtr,
                                                     pluginManager, pluginWindowManager,
                                                     pluginScanner));
        strip->onTrackSelected = [this] (te::AudioTrack* t) { setMixerSelectedTrack (t); };

        // Bus-group propagation: after a solo/mute button click, find all tracks on the
        // same bus and apply the same new state so the entire bus group moves together.
        strip->onSoloClicked = [this, strip]
        {
            const bool newState = strip->getTrack().isSolo (false);
            for (auto* sibling : getTracksInSameBusGroup (strip->getTrack()))
                if (sibling != &strip->getTrack())
                    mixerEngine.setSolo (*sibling, newState);
            refreshAllStrips();
        };
        strip->onMuteClicked = [this, strip]
        {
            const bool newState = strip->getTrack().isMuted (false);
            for (auto* sibling : getTracksInSameBusGroup (strip->getTrack()))
                if (sibling != &strip->getTrack())
                    mixerEngine.setMute (*sibling, newState);
            refreshAllStrips();
        };

        // Multi-selection routing: when one selected strip is routed to a bus (or Master),
        // apply the same routing to all other selected strips simultaneously.
        strip->onOutputRouted = [this, strip] (te::AudioTrack* targetBus)
        {
            for (auto* sibling : selectedStrips)
            {
                if (sibling == strip) continue;  // already routed by the strip itself
                if (targetBus == nullptr)
                    mixerEngine.unrouteFromBus (sibling->getTrack());
                else
                    mixerEngine.routeTrackToBus (sibling->getTrack(), *targetBus);
            }
            refreshAllStrips();
        };

        // Multi-selection group fader: drag one selected strip's fader → all selected
        // strips move by the same dB delta (relative, preserving balance).
        strip->onVolumeDragStart = [this, strip] { beginFaderGroup (strip); };
        strip->onVolumeChanged   = [this, strip] (double db) { applyFaderGroup (strip, db); };
        strip->onVolumeDragEnd   = [this] { endFaderGroup(); };

        channelStripContainer.addAndMakeVisible (strip);
    }

    // ---- Bus and FX channel strips ----
    // Order: Bus channels first, then FX channels (Studio One default)
    auto addBusFXStrip = [&] (te::AudioTrack* audioTrack)
    {
        auto meter = std::make_unique<MeterProcessor>();
        meter->prepareToPlay (44100.0, 512);
        auto* meterPtr = meter.get();
        meterProcessors[audioTrack] = std::move (meter);

        auto client = std::make_unique<te::LevelMeasurer::Client>();
        if (auto* levelPlugin = audioTrack->getLevelMeterPlugin())
            levelPlugin->measurer.addClient (*client);
        levelClients[audioTrack] = std::move (client);

        auto* strip = busFXStrips.add (new BusFXStrip (*audioTrack, mixerEngine, *meterPtr,
                                                        pluginManager, pluginWindowManager,
                                                        pluginScanner));
        strip->onTrackSelected = [this] (te::AudioTrack* t) { setMixerSelectedTrack (t); };
        channelStripContainer.addAndMakeVisible (strip);
    };

    for (auto* audioTrack : te::getAudioTracks (*edit))
        if (MixerEngine::isBusChannel (*audioTrack))
            addBusFXStrip (audioTrack);

    for (auto* audioTrack : te::getAudioTracks (*edit))
        if (MixerEngine::isFXChannel (*audioTrack))
            addBusFXStrip (audioTrack);

    // ---- VCA strips (group faders, after Bus/FX, before Master) ----
    for (auto* audioTrack : te::getAudioTracks (*edit))
    {
        if (! MixerEngine::isVCAChannel (*audioTrack))
            continue;
        auto* strip = vcaStrips.add (new VCAStrip (*audioTrack, mixerEngine));
        strip->onTrackSelected  = [this] (te::AudioTrack* t) { setMixerSelectedTrack (t); };
        strip->onMembersChanged = [this] { refreshAllStrips(); };
        channelStripContainer.addAndMakeVisible (strip);
    }

    // ---- Master strip (rightmost, pinned outside viewport) ----
    masterStrip = std::make_unique<MasterStrip> (
        *edit,
        appState.audioEngine.getMasterMeterProcessor(),
        pluginWindowManager,
        pluginScanner);
    addAndMakeVisible (*masterStrip);

    // Rebuild send slots on all audio strips from current FX channels
    rebuildAllSends();

    resized();
}

void MixerView::rebuildAllSends()
{
    const auto fxChannels = mixerEngine.getFXChannels();
    for (auto* strip : strips)
        strip->rebuildSends (fxChannels);
}

//==============================================================================
// Track selection — used by S/M/R shortcuts when Mixer has keyboard focus

void MixerView::setMixerSelectedTrack (te::AudioTrack* t)
{
    if (t == nullptr)
    {
        selectedMixerTracks.clear();
        selectedStrips.clear();
        selectedBusFXStrips.clear();
        for (auto* s : strips)     s->setSelected (false);
        for (auto* s : busFXStrips) s->setSelected (false);
        for (auto* s : vcaStrips)   s->setSelected (false);
        grabKeyboardFocus();
        if (onSelectionForwarded)
            onSelectionForwarded (nullptr);
        return;
    }

    const bool cmdDown = juce::ModifierKeys::getCurrentModifiers().isCommandDown();

    if (cmdDown)
    {
        // Toggle: add to selection or remove from it
        if (selectedMixerTracks.contains (t))
            selectedMixerTracks.removeAllInstancesOf (t);
        else
            selectedMixerTracks.addIfNotAlreadyThere (t);
    }
    else
    {
        // Single selection — clear everything first
        selectedMixerTracks.clearQuick();
        selectedMixerTracks.add (t);
    }

    // Rebuild strip selection arrays and update visual highlight on all strips
    selectedStrips.clearQuick();
    selectedBusFXStrips.clearQuick();

    for (auto* s : strips)
    {
        const bool sel = selectedMixerTracks.contains (&s->getTrack());
        s->setSelected (sel);
        if (sel) selectedStrips.add (s);
    }
    for (auto* s : busFXStrips)
    {
        const bool sel = selectedMixerTracks.contains (&s->getTrack());
        s->setSelected (sel);
        if (sel) selectedBusFXStrips.add (s);
    }
    for (auto* s : vcaStrips)
        s->setSelected (selectedMixerTracks.contains (&s->getTrack()));

    grabKeyboardFocus();

    // Mirror the primary selection into the arrangement (set by MainComponent)
    if (onSelectionForwarded)
        onSelectionForwarded (selectedMixerTracks.isEmpty() ? nullptr : t);
}

bool MixerView::keyPressed (const juce::KeyPress& key)
{
    // Cmd+A — select all strips regardless of other state
    if (key.getModifiers().isCommandDown()
        && (key.getKeyCode() == 'a' || key.getKeyCode() == 'A'))
    {
        selectedMixerTracks.clear();
        selectedStrips.clear();
        selectedBusFXStrips.clear();
        for (auto* s : strips)
        {
            s->setSelected (true);
            selectedMixerTracks.addIfNotAlreadyThere (&s->getTrack());
            selectedStrips.add (s);
        }
        for (auto* s : busFXStrips)
        {
            s->setSelected (true);
            selectedMixerTracks.addIfNotAlreadyThere (&s->getTrack());
            selectedBusFXStrips.add (s);
        }
        return true;
    }

    // No-selection fallback: S/M with no modifier and nothing selected → apply to all regular strips
    if (selectedMixerTracks.isEmpty() && ! key.getModifiers().isAnyModifierKeyDown() && ! strips.isEmpty())
    {
        if (key.getKeyCode() == 's' || key.getKeyCode() == 'S')
        {
            bool anySoloed = false;
            for (auto* s : strips) if (s->getTrack().isSolo (false)) { anySoloed = true; break; }
            for (auto* s : strips) mixerEngine.setSolo (s->getTrack(), ! anySoloed);
            refreshAllStrips();
            return true;
        }
        if (key.getKeyCode() == 'm' || key.getKeyCode() == 'M')
        {
            bool anyUnmuted = false;
            for (auto* s : strips) if (! s->getTrack().isMuted (false)) { anyUnmuted = true; break; }
            for (auto* s : strips) mixerEngine.setMute (s->getTrack(), anyUnmuted);
            refreshAllStrips();
            return true;
        }
    }

    // S/M/R shortcuts — require at least one selected track and no modifier keys
    if (selectedMixerTracks.isEmpty() || key.getModifiers().isAnyModifierKeyDown())
        return false;

    if (key.getKeyCode() == 's' || key.getKeyCode() == 'S')
    {
        for (auto* t : selectedMixerTracks)
        {
            const bool newSolo = ! t->isSolo (false);
            mixerEngine.setSolo (*t, newSolo);

            // Bus-group propagation: apply same solo to all tracks on the same bus
            for (auto* sibling : getTracksInSameBusGroup (*t))
                if (sibling != t)
                    mixerEngine.setSolo (*sibling, newSolo);
        }
        refreshAllStrips();
        return true;
    }
    if (key.getKeyCode() == 'm' || key.getKeyCode() == 'M')
    {
        for (auto* t : selectedMixerTracks)
        {
            const bool newMute = ! t->isMuted (false);
            mixerEngine.setMute (*t, newMute);

            // Bus-group propagation: apply same mute to all tracks on the same bus
            for (auto* sibling : getTracksInSameBusGroup (*t))
                if (sibling != t)
                    mixerEngine.setMute (*sibling, newMute);
        }
        refreshAllStrips();
        return true;
    }
    if (key.getKeyCode() == 'r' || key.getKeyCode() == 'R')
    {
        // Only arm/disarm regular audio tracks — BusFXStrips are not record tracks.
        for (auto* strip : selectedStrips)
        {
            auto* t = &strip->getTrack();
            auto& edit = t->edit;
            const bool newArmed = ! (bool) t->state.getProperty ("armed", false);
            t->state.setProperty ("armed", newArmed, nullptr);

            if (newArmed)
            {
                // Assign first enabled wave input if none already assigned (Studio One style)
                bool alreadyAssigned = false;
                for (auto* instance : edit.getAllInputDevices())
                {
                    if (te::isOnTargetTrack (*instance, *t, 0))
                    {
                        alreadyAssigned = true;
                        instance->setRecordingEnabled (t->itemID, true);
                    }
                }
                if (! alreadyAssigned)
                {
                    auto& dm = edit.engine.getDeviceManager();
                    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
                    {
                        auto* waveIn = dm.getWaveInDevice (i);
                        if (waveIn == nullptr || ! waveIn->isEnabled()) continue;
                        for (auto* instance : edit.getAllInputDevices())
                        {
                            if (&instance->getInputDevice() == waveIn)
                            {
                                auto assignResult = instance->setTarget (t->itemID, true, nullptr);
                                if (assignResult.has_value())
                                    instance->setRecordingEnabled (t->itemID, true);
                                break;
                            }
                        }
                        break;
                    }
                }
            }
            else
            {
                // Disarm — disable recording on all instances targeting this track
                for (auto* instance : edit.getAllInputDevices())
                    if (te::isOnTargetTrack (*instance, *t, 0))
                        instance->setRecordingEnabled (t->itemID, false);
            }
        }
        refreshAllSelectedStrips();
        return true;
    }
    return false;
}

//==============================================================================
// juce::Timer — 24fps meter update

void MixerView::timerCallback()
{
    // Flush master meter FIFO and push values to the Master strip.
    auto& masterMeter = appState.audioEngine.getMasterMeterProcessor();
    masterMeter.flushFifo();

    if (masterStrip != nullptr)
    {
        const float mPeakDB = masterMeter.readAndResetPeakDB();
        const float mRmsDB  = masterMeter.getRMSDB();
        const float mLufsM  = masterMeter.getLUFSMomentary();
        const float mLufsI  = masterMeter.getLUFSIntegrated();
        masterStrip->setMeterValues (mPeakDB, mRmsDB, mLufsM, mLufsI);
    }

    // ---- Per-track metering (audio + bus/fx strips) ----
    static juce::AudioBuffer<float> synthBuf (2, 512);
    juce::MidiBuffer midiDummy;

    // Helper: update any strip that has a track with a level client
    auto updateStrip = [&] (te::AudioTrack& audioTrack, auto* strip)
    {
        float peakDB = getArmedInputPeakDB (audioTrack);

        if (peakDB <= -100.0f)
        {
            auto it = levelClients.find (&audioTrack);
            if (it != levelClients.end())
            {
                const auto levelL = it->second->getAndClearAudioLevel (0);
                const auto levelR = it->second->getAndClearAudioLevel (1);
                peakDB = juce::jmax (levelL.dB, levelR.dB);
            }
        }

        auto it = meterProcessors.find (&audioTrack);
        if (it != meterProcessors.end())
        {
            auto* meter = it->second.get();

            const float linearAmp = (peakDB > -100.0f)
                                    ? std::pow (10.0f, peakDB / 20.0f)
                                    : 0.0f;
            synthBuf.clear();
            for (int ch = 0; ch < synthBuf.getNumChannels(); ++ch)
                juce::FloatVectorOperations::fill (synthBuf.getWritePointer (ch),
                                                   linearAmp,
                                                   synthBuf.getNumSamples());

            meter->processBlock (synthBuf, midiDummy);
            meter->flushFifo();

            strip->setMeterValues (peakDB,
                                   meter->getRMSDB(),
                                   meter->getLUFSMomentary(),
                                   meter->getLUFSIntegrated());
        }
        else
        {
            strip->setMeterValues (peakDB, peakDB - 3.0f, peakDB - 12.0f, peakDB - 16.0f);
        }
    };

    for (auto* strip : strips)
        updateStrip (strip->getTrack(), strip);

    for (auto* strip : busFXStrips)
        updateStrip (strip->getTrack(), strip);
}

void MixerView::detachInputLevelClients()
{
    for (auto& [track, entry] : inputLevelClients)
    {
        juce::ignoreUnused (track);

        if (entry.device != nullptr && entry.client != nullptr)
            entry.device->levelMeasurer.removeClient (*entry.client);
    }

    inputLevelClients.clear();
}

float MixerView::getArmedInputPeakDB (te::AudioTrack& track)
{
    if (! (bool) track.state.getProperty ("armed", false))
        return -100.0f;

    te::WaveInputDevice* assignedDevice = nullptr;

    for (auto* instance : track.edit.getAllInputDevices())
    {
        if (instance == nullptr || ! te::isOnTargetTrack (*instance, track, 0))
            continue;

        if (auto* waveIn = dynamic_cast<te::WaveInputDevice*> (&instance->getInputDevice()))
        {
            assignedDevice = waveIn;
            break;
        }
    }

    if (assignedDevice == nullptr)
        return -100.0f;

    auto& entry = inputLevelClients[&track];

    if (entry.device != assignedDevice)
    {
        if (entry.device != nullptr && entry.client != nullptr)
            entry.device->levelMeasurer.removeClient (*entry.client);

        entry.device = assignedDevice;
        entry.client = std::make_unique<te::LevelMeasurer::Client>();
        entry.device->levelMeasurer.addClient (*entry.client);
    }

    if (entry.client == nullptr)
        return -100.0f;

    float peakDB = -100.0f;
    const int numChannels = juce::jlimit (1, (int) te::LevelMeasurer::Client::maxNumChannels,
                                          entry.client->getNumChannelsUsed());

    for (int ch = 0; ch < numChannels; ++ch)
        peakDB = juce::jmax (peakDB, entry.client->getAndClearAudioLevel (ch).dB);

    if (peakDB <= -100.0f)
    {
        for (const auto& channel : assignedDevice->getChannels())
            peakDB = juce::jmax (peakDB, appState.audioEngine.getAndClearInputPeakDB (channel.indexInDevice));
    }

    return peakDB;
}

//==============================================================================
// Strip refresh helpers

void MixerView::refreshAllSelectedStrips()
{
    for (auto* s : selectedStrips)     s->refresh();
    for (auto* s : selectedBusFXStrips) s->refresh();
}

void MixerView::refreshAllStrips()
{
    for (auto* s : strips)      s->refresh();
    for (auto* s : busFXStrips) s->refresh();
    for (auto* s : vcaStrips)   s->refresh();
}

//==============================================================================
// Multi-selection group fader (Studio One-style relative move)

void MixerView::beginFaderGroup (ChannelStrip* anchor)
{
    faderGroupActive = false;
    faderGroupStart.clear();

    // Only group-move when the dragged strip is part of a multi-selection.
    if (anchor == nullptr || ! selectedStrips.contains (anchor) || selectedStrips.size() < 2)
        return;

    faderGroupActive      = true;
    faderGroupAnchorStart = mixerEngine.getVolume (anchor->getTrack());
    for (auto* s : selectedStrips)
        faderGroupStart[s] = mixerEngine.getVolume (s->getTrack());
}

void MixerView::applyFaderGroup (ChannelStrip* anchor, double newDb)
{
    if (! faderGroupActive)
        return;

    // The anchor already moved via its own volumeChanged(); apply the same delta
    // to every other selected strip, clamped to the fader range, preserving balance.
    const double delta = newDb - faderGroupAnchorStart;
    for (auto* s : selectedStrips)
    {
        if (s == anchor)
            continue;

        auto it = faderGroupStart.find (s);
        if (it == faderGroupStart.end())
            continue;

        const double target = juce::jlimit (-60.0, 6.0, it->second + delta);
        mixerEngine.setVolume (s->getTrack(), (float) target);  // merges into anchor's undo txn
        s->refresh();                                            // sync the sibling's fader
    }
}

void MixerView::endFaderGroup()
{
    faderGroupActive = false;
    faderGroupStart.clear();
}

juce::Array<te::AudioTrack*> MixerView::getTracksInSameBusGroup (te::AudioTrack& t) const
{
    juce::Array<te::AudioTrack*> result;
    auto* bus = mixerEngine.getBusChannelFor (t);
    if (bus == nullptr)
        return result;  // no bus → no group

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return result;

    for (auto* audioTrack : te::getAudioTracks (*edit))
        if (MixerEngine::isAudioChannel (*audioTrack)
            && mixerEngine.getBusChannelFor (*audioTrack) == bus)
            result.add (audioTrack);

    return result;
}

//==============================================================================
// ValueTree::Listener

void MixerView::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (auto* edit = appState.editManager.getEdit())
    {
        if (parent == edit->state || parent.isAChildOf (edit->state))
            juce::MessageManager::callAsync ([this] { rebuildStrips(); });
    }
    juce::ignoreUnused (parent, child);
}

void MixerView::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (auto* edit = appState.editManager.getEdit())
    {
        if (parent == edit->state || parent.isAChildOf (edit->state))
            juce::MessageManager::callAsync ([this] { rebuildStrips(); });
    }
    juce::ignoreUnused (parent, child);
}

void MixerView::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    if (auto* edit = appState.editManager.getEdit())
    {
        if (parent == edit->state || parent.isAChildOf (edit->state))
            juce::MessageManager::callAsync ([this] { rebuildStrips(); });
    }
    juce::ignoreUnused (parent);
}
