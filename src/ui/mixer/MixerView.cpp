#include "MixerView.h"
#include "ui/pluginbrowser/PluginBrowserPopup.h"

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

    // Register ValueTree listener so we rebuild on track add/remove
    if (auto* edit = appState.editManager.getEdit())
        edit->state.addListener (this);

    rebuildStrips();

    // Start the 24fps meter update timer (plan 01-17 — EXP-03 gap closure).
    // timerCallback() flushes the master MeterProcessor FIFO and pushes values
    // to all channel strip MeterBars.
    startTimerHz (24);
}

MixerView::~MixerView()
{
    stopTimer();  // Stop before destroying strips to avoid dangling timerCallback

    if (auto* edit = appState.editManager.getEdit())
        edit->state.removeListener (this);

    // Clear strips first (they reference MeterProcessors); then clear MeterProcessors
    strips.clear();
    meterProcessors.clear();
}

void MixerView::resized()
{
    channelStripViewport.setBounds (getLocalBounds());

    // Compute candidate strip height from current geometry.
    // Use MixerView's own height (set by MainComponent::resized, stable).
    const int viewH = getHeight();

    // Only update cached height if we have a meaningful parent-assigned height.
    // During viewport internal relayout, getHeight() may be stale or zero.
    if (viewH > 100)
    {
        const int scrollBarH = channelStripViewport.getScrollBarThickness();
        cachedStripHeight = juce::jmax (200, viewH - scrollBarH);
    }

    const int stripH = cachedStripHeight;

    // Size container to hold all strips side-by-side (audio + bus)
    const int totalW = juce::jmax ((strips.size() + busStrips.size()) * kStripWidth,
                                   channelStripViewport.getWidth());
    channelStripContainer.setSize (totalW, stripH);

    // Lay out audio track strips horizontally
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * kStripWidth, 0, kStripWidth, stripH);

    // Lay out bus strips after audio track strips
    for (int i = 0; i < busStrips.size(); ++i)
    {
        const int busIdx = strips.size() + i;
        busStrips[i]->setBounds (busIdx * kStripWidth, 0, kStripWidth, stripH);
    }
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));

    // Top border separating mixer from arrangement
    g.setColour (juce::Colour (0xff000000));
    g.drawLine (0.0f, 0.0f, (float)getWidth(), 0.0f, 2.0f);
}

MeterProcessor* MixerView::getMeterProcessorForTrack (const te::AudioTrack& track)
{
    auto it = meterProcessors.find (const_cast<te::AudioTrack*> (&track));
    return (it != meterProcessors.end()) ? it->second.get() : nullptr;
}

void MixerView::rebuildStrips()
{
    // Clear existing strips and processors
    channelStripContainer.removeAllChildren();
    strips.clear();
    busStrips.clear();
    meterProcessors.clear();

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    for (auto* audioTrack : te::getAudioTracks (*edit))
    {
        // Create a MeterProcessor for this track
        auto meter = std::make_unique<MeterProcessor>();
        // Initialise with nominal values; audio engine will call prepareToPlay
        // with real values when it starts.
        meter->prepareToPlay (44100.0, 512);

        auto* meterPtr = meter.get();
        meterProcessors[audioTrack] = std::move (meter);

        // Create the ChannelStrip passing MeterProcessor and plugin management references
        auto* strip = strips.add (new ChannelStrip (*audioTrack, mixerEngine, *meterPtr,
                                                     pluginManager, pluginWindowManager,
                                                     pluginScanner));
        channelStripContainer.addAndMakeVisible (strip);
    }

    // Create BusStrips for FolderTracks (buses created via "Create Bus from Track")
    for (auto* track : te::getAllTracks (*edit))
    {
        if (auto* folderTrack = dynamic_cast<te::FolderTrack*> (track))
        {
            auto* busStrip = busStrips.add (new BusStrip (*folderTrack,
                                                           pluginManager,
                                                           pluginWindowManager,
                                                           pluginScanner));
            channelStripContainer.addAndMakeVisible (busStrip);
        }
    }

    resized();
}

//==============================================================================
// BusStrip — context menu and plugin insert (plan 01-27, MIX-02/PLUG-04 gap closure)

void MixerView::BusStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showContextMenu();
}

void MixerView::BusStrip::showContextMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Add Plugin...");
    menu.addSeparator();
    menu.addItem (2, "Rename Bus");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this] (int result)
        {
            if (result == 1)
            {
                PluginBrowserPopup::showForComponent (
                    *this,
                    pluginScanner.getKnownPlugins(),
                    [this] (const juce::PluginDescription& desc)
                    {
                        auto* newPlugin = pluginManager.insertPlugin (track, desc);
                        if (newPlugin != nullptr)
                        {
                            pluginWindowManager.openPluginWindow (*newPlugin);
                            insertSlotPanel.refresh();
                        }
                    });
            }
            else if (result == 2)
            {
                // JUCE 8: showInputBoxAsync removed — build AlertWindow manually
                // and use showAsync (JUCE_MODAL_LOOPS_PERMITTED not set).
                auto* alert = new juce::AlertWindow ("Rename Bus",
                                                     "New name:",
                                                     juce::MessageBoxIconType::QuestionIcon);
                alert->addTextEditor ("name", track.getName());
                alert->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
                alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

                alert->enterModalState (true,
                    juce::ModalCallbackFunction::create (
                        [alert, this] (int code)
                        {
                            if (code == 1)
                            {
                                const auto name = alert->getTextEditorContents ("name");
                                if (name.isNotEmpty())
                                    track.setName (name);
                            }
                        }),
                    true /* delete on dismiss */);
            }
        });
}

//==============================================================================
// juce::Timer — 24fps meter update (plan 01-17, EXP-03 gap closure)

void MixerView::timerCallback()
{
    // Get the master MeterProcessor from AudioEngine.
    // This processor taps the post-render output buffer via an AudioIODeviceCallback,
    // so it receives real audio data during playback.
    auto& masterMeter = appState.audioEngine.getMasterMeterProcessor();

    // CRITICAL: flushFifo() pumps the lock-free FIFO into libebur128 on the message thread.
    // This call must happen on the message thread (which it does — we are in timerCallback).
    // The audio thread NEVER calls libebur128 — it only writes to the FIFO.
    masterMeter.flushFifo();

    // Read-and-reset peak so each 24fps window shows only this interval's max,
    // not an accumulated max since program start (fixes ratchet-upward bug).
    const float peakDB = masterMeter.readAndResetPeakDB();
    const float rmsDB  = masterMeter.getRMSDB();
    const float lufsM  = masterMeter.getLUFSMomentary();
    const float lufsI  = masterMeter.getLUFSIntegrated();

    // Push master meter values to all channel strip MeterBars.
    // Phase 1: all strips show the same program-level mix.
    for (auto* strip : strips)
        strip->setMeterValues (peakDB, rmsDB, lufsM, lufsI);
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
