#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "fx/EQProcessor.h"

/**
 * SpectrumAnalyzer — Real-time FFT spectrum display that reads from EQProcessor.
 *
 * Architecture (RESEARCH.md Pitfall 6: FFT on message thread, NOT audio thread):
 *   - EQProcessor::processBlock() writes samples into a lock-free FIFO
 *   - timerCallback() runs on the message thread at ~24fps (40ms interval)
 *   - timerCallback() reads the FIFO via getSpectrumData() and repaints
 *   - No FFT computation happens on the audio thread
 *
 * Visual style: filled spectrum area drawn behind the EQ curve.
 * Frequency range: 20Hz – 20kHz (log scale x-axis)
 * Magnitude range: -90dB to 0dBFS (linear y-axis)
 *
 * Used as a child component of EQComponent (rendered first, behind the curve).
 */
class SpectrumAnalyzer : public juce::Component,
                         private juce::Timer
{
public:
    explicit SpectrumAnalyzer (EQProcessor& eqProcessor);
    ~SpectrumAnalyzer() override;

    void paint (juce::Graphics& g) override;

    // Coordinate conversions — public so EQComponent can share the same scale
    float xForFrequency (float hz) const;
    float yForMagnitudeDB (float dB) const;

private:
    EQProcessor&        eqProcessor;
    std::vector<float>  spectrumMagnitudes;  // In dB, updated each timer tick

    static constexpr int kFFTSize = 2048;

    // Timer runs on message thread — reads lock-free FIFO from EQProcessor
    void timerCallback() override;  // Fires at ~24fps (40ms)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzer)
};
