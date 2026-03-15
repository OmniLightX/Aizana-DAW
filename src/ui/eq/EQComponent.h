#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "fx/EQProcessor.h"
#include "SpectrumAnalyzer.h"

/**
 * EQComponent — Professional parametric EQ UI (FabFilter Pro-Q / Logic Channel EQ style).
 *
 * Visual layout:
 *   - Dark background with frequency grid lines (decade markers: 100Hz, 1kHz, 10kHz)
 *   - dB grid lines at ±3, ±6, ±12dB
 *   - SpectrumAnalyzer rendered behind the EQ curve (CONTEXT.md requirement)
 *   - EQ magnitude response curve drawn in white/light blue
 *   - Per-band drag handles (circles) on the curve
 *
 * Interaction (CONTEXT.md: draggable handles):
 *   - Vertical drag: change band gain (±18dB range)
 *   - Horizontal drag: change band frequency (20Hz – 20kHz, log scale)
 *   - Ctrl + vertical drag: change Q (0.1 to 10.0)
 *   - Double-click on handle: reset band to flat (0dB, default Q)
 *   - Curve updates in real-time during drag (calls EQProcessor::setBand)
 *
 * Coordinate system: log frequency x-axis, linear dB y-axis.
 * Both EQComponent and SpectrumAnalyzer share the same coordinate transforms.
 */
class EQComponent : public juce::Component
{
public:
    explicit EQComponent (EQProcessor& eqProcessor);
    ~EQComponent() override = default;

    void resized() override;
    void paint (juce::Graphics& g) override;

    void mouseDown  (const juce::MouseEvent& e) override;
    void mouseDrag  (const juce::MouseEvent& e) override;
    void mouseUp    (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    EQProcessor&    eqProcessor;
    SpectrumAnalyzer spectrumAnalyzer;  // Child component, rendered behind curve

    // Per-band drag state
    struct BandHandle
    {
        bool isDragging = false;
    };
    std::array<BandHandle, EQProcessor::NumBands> bandHandles;

    int draggingBandIndex = -1;
    juce::Point<float> dragStartPos;
    EQBand dragStartBand;

    // Handle radius in pixels
    static constexpr float kHandleRadius = 7.0f;

    // -------------------------------------------------------------------------
    // Coordinate conversions (log frequency x, linear dB y)

    float xForFrequency  (float hz) const;
    float yForGainDB     (float dB) const;
    float frequencyAtX   (float x)  const;
    float gainDBAtY      (float y)  const;

    // -------------------------------------------------------------------------
    // Drawing

    /** Draws frequency/dB grid lines and labels. */
    void drawGrid (juce::Graphics& g) const;

    /** Draws the combined EQ magnitude response curve computed analytically. */
    void drawEQCurve (juce::Graphics& g) const;

    /** Draws draggable band handles at each band's (freq, gain) position. */
    void drawBandHandles (juce::Graphics& g) const;

    /**
     * Returns the combined magnitude response at frequency hz (in dB).
     * Computed analytically from each band's IIR coefficients using
     * juce::dsp::IIR::Coefficients::getMagnitudeForFrequency().
     */
    float getMagnitudeAtFrequency (float hz) const;

    /**
     * Returns the screen-space position of band handle n.
     * Based on the band's current frequency and gain settings.
     */
    juce::Point<float> bandHandlePosition (int bandIndex) const;

    /**
     * Hit-test: returns band index (0-3) if the given position is within
     * kHandleRadius of any band handle, or -1 if no hit.
     */
    int hitTestBandHandle (juce::Point<float> pos) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQComponent)
};
