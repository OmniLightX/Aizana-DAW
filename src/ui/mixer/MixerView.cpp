#include "MixerView.h"

//==============================================================================

MixerView::MixerView (ApplicationState& appState_, MixerEngine& mixerEngine_)
    : appState (appState_)
    , mixerEngine (mixerEngine_)
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

    // Size container to hold all strips side-by-side
    const int totalW = juce::jmax (strips.size() * kStripWidth,
                                   channelStripViewport.getWidth());
    channelStripContainer.setSize (totalW, channelStripViewport.getMaximumVisibleHeight());

    // Lay out strips horizontally
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * kStripWidth, 0, kStripWidth,
                              channelStripContainer.getHeight());
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

        // Create the ChannelStrip passing the MeterProcessor reference
        auto* strip = strips.add (new ChannelStrip (*audioTrack, mixerEngine, *meterPtr));
        channelStripContainer.addAndMakeVisible (strip);
    }

    resized();
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

    // Read the updated values (atomics — thread-safe from any thread)
    const float peakDB = masterMeter.getPeakDB();
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
