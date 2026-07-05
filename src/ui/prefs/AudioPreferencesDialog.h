#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "audio/AudioEngine.h"

/**
 * AudioPreferencesDialog — Full audio device setup via JUCE's AudioDeviceSelectorComponent.
 *
 * Embeds juce::AudioDeviceSelectorComponent connected directly to TE's AudioDeviceManager.
 * Device selection, sample rate, buffer size, and MIDI routing are handled internally
 * by JUCE/TE — no custom XML persistence or reinitialise() required.
 *
 * Opened via: Aizana > Preferences... (Cmd+,)
 *
 * Latency display auto-refreshes every 500ms via juce::Timer.
 */
class AudioPreferencesDialog : public juce::Component,
                                private juce::Timer
{
public:
    explicit AudioPreferencesDialog (AudioEngine& engine);
    ~AudioPreferencesDialog() override;

    void resized() override;
    void paint   (juce::Graphics& g) override;

    static void show (AudioEngine& engine);

private:
    AudioEngine& audioEngine;

    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::Label                        latencyLabel;

    void timerCallback() override;
    void updateLatencyLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPreferencesDialog)
};
