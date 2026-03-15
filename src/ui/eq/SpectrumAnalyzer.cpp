#include "SpectrumAnalyzer.h"

//==============================================================================

SpectrumAnalyzer::SpectrumAnalyzer (EQProcessor& eqProcessor_)
    : eqProcessor (eqProcessor_)
{
    spectrumMagnitudes.resize (kFFTSize / 2, -90.0f);
    startTimerHz (24);  // ~24fps (~40ms interval) — RESEARCH.md Pitfall 6
}

SpectrumAnalyzer::~SpectrumAnalyzer()
{
    stopTimer();
}

//==============================================================================
// Coordinate conversions

float SpectrumAnalyzer::xForFrequency (float hz) const
{
    // Log frequency scale: 20Hz at left, 20kHz at right
    static constexpr float kMinFreq = 20.0f;
    static constexpr float kMaxFreq = 20000.0f;
    const float logMin = std::log10 (kMinFreq);
    const float logMax = std::log10 (kMaxFreq);
    const float t = (std::log10 (juce::jlimit (kMinFreq, kMaxFreq, hz)) - logMin) / (logMax - logMin);
    return t * (float)getWidth();
}

float SpectrumAnalyzer::yForMagnitudeDB (float dB) const
{
    // -90dB at bottom, 0dBFS at top
    static constexpr float kMinDB = -90.0f;
    static constexpr float kMaxDB =   0.0f;
    const float t = 1.0f - juce::jlimit (0.0f, 1.0f, (dB - kMinDB) / (kMaxDB - kMinDB));
    return t * (float)getHeight();
}

//==============================================================================
// Timer callback — runs on message thread, reads lock-free FIFO

void SpectrumAnalyzer::timerCallback()
{
    // EQProcessor::getSpectrumData() reads the lock-free FIFO and runs FFT.
    // Returns false if no new data since the last call.
    if (eqProcessor.getSpectrumData (spectrumMagnitudes, kFFTSize))
        repaint();
}

//==============================================================================
// Paint

void SpectrumAnalyzer::paint (juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    const int numBins = (int)spectrumMagnitudes.size();
    if (numBins == 0)
        return;

    // Sample rate used for bin-to-frequency mapping
    // We use a nominal 44100; the EQProcessor will have the real SR but for
    // display purposes the log-frequency mapping is perceptually indistinguishable.
    constexpr float kNominalSampleRate = 44100.0f;
    const float binHz = kNominalSampleRate / (float)(kFFTSize);

    juce::Path spectrumPath;
    bool pathStarted = false;

    for (int bin = 1; bin < numBins; ++bin)
    {
        const float hz = (float)bin * binHz;
        if (hz < 20.0f || hz > 20000.0f)
            continue;

        const float x = xForFrequency (hz);
        const float y = yForMagnitudeDB (spectrumMagnitudes[bin]);

        if (!pathStarted)
        {
            spectrumPath.startNewSubPath (x, (float)getHeight());
            spectrumPath.lineTo (x, y);
            pathStarted = true;
        }
        else
        {
            spectrumPath.lineTo (x, y);
        }
    }

    if (pathStarted)
    {
        spectrumPath.lineTo ((float)getWidth(), (float)getHeight());
        spectrumPath.closeSubPath();

        // Filled spectrum area — translucent cyan-green
        g.setColour (juce::Colour (0x2200c8a0));
        g.fillPath (spectrumPath);

        // Spectrum outline
        g.setColour (juce::Colour (0x6600c8a0));
        g.strokePath (spectrumPath, juce::PathStrokeType (1.0f));
    }
}
