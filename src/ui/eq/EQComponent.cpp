#include "EQComponent.h"
#include <juce_dsp/juce_dsp.h>

//==============================================================================

EQComponent::EQComponent (EQProcessor& eqProcessor_)
    : eqProcessor  (eqProcessor_)
    , spectrumAnalyzer (eqProcessor_)
{
    // SpectrumAnalyzer is a child — rendered first (behind EQ curve).
    // Must NOT intercept mouse events — EQComponent handles all mouse interaction
    // for band handle dragging. Without this, SpectrumAnalyzer (which fills 100% of
    // our bounds) would steal every mouseDown before it reaches EQComponent::mouseDown.
    addAndMakeVisible (spectrumAnalyzer);
    spectrumAnalyzer.setInterceptsMouseClicks (false, false);

    // Drag readout label — dark pill shown near dragging handle
    dragInfoLabel.setFont (juce::FontOptions (10.0f));
    dragInfoLabel.setJustificationType (juce::Justification::centred);
    dragInfoLabel.setColour (juce::Label::textColourId,       juce::Colours::white);
    dragInfoLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xdd111111));
    dragInfoLabel.setVisible (false);
    addChildComponent (dragInfoLabel);

    setSize (700, 350);

    // F4: die dynamische Kurve muss sichtbar ATMEN. 30 Hz ist die Grenze, ab der
    // eine Regelbewegung fluessig wirkt; darunter sieht sie ruckartig aus und man
    // traut ihr nicht. Der Timer zeichnet nur, wenn wirklich geregelt wird.
    startTimerHz (30);
}

//==============================================================================
// Layout

void EQComponent::resized()
{
    // SpectrumAnalyzer fills the entire component (painted behind the curve)
    spectrumAnalyzer.setBounds (getLocalBounds());
}

//==============================================================================
// Coordinate conversions

float EQComponent::xForFrequency (float hz) const
{
    constexpr float kMinFreq = 20.0f;
    constexpr float kMaxFreq = 20000.0f;
    const float logMin = std::log10 (kMinFreq);
    const float logMax = std::log10 (kMaxFreq);
    const float t = (std::log10 (juce::jlimit (kMinFreq, kMaxFreq, hz)) - logMin) / (logMax - logMin);
    return t * (float)getWidth();
}

float EQComponent::yForGainDB (float dB) const
{
    // ±18dB range: 0dB at vertical centre
    constexpr float kRange = 18.0f;
    const float t = 0.5f - dB / (2.0f * kRange);
    return juce::jlimit (0.0f, (float)getHeight(), t * (float)getHeight());
}

float EQComponent::frequencyAtX (float x) const
{
    constexpr float kMinFreq = 20.0f;
    constexpr float kMaxFreq = 20000.0f;
    const float logMin = std::log10 (kMinFreq);
    const float logMax = std::log10 (kMaxFreq);
    const float t = x / (float)getWidth();
    return std::pow (10.0f, logMin + t * (logMax - logMin));
}

float EQComponent::gainDBAtY (float y) const
{
    constexpr float kRange = 18.0f;
    const float t = y / (float)getHeight();
    return (0.5f - t) * 2.0f * kRange;
}

//==============================================================================
// Magnitude response

float EQComponent::getMagnitudeAtFrequency (float hz) const
{
    return magnitudeAt (hz, false);
}

float EQComponent::getDynamicMagnitudeAtFrequency (float hz) const
{
    return magnitudeAt (hz, true);
}

bool EQComponent::anyDynamicsActive() const
{
    for (int i = 0; i < EQProcessor::NumBands; ++i)
        if (eqProcessor.isDynamicsActive (i))
            return true;
    return false;
}

void EQComponent::timerCallback()
{
    // Nur neu zeichnen, wenn wirklich geregelt wird — ein EQ ohne Dynamik soll
    // keine 30 Repaints pro Sekunde kosten.
    if (anyDynamicsActive())
        repaint();
}

float EQComponent::magnitudeAt (float hz, bool withDynamics) const
{
    // Accumulate gain contributions from all enabled bands.
    // Each band's contribution is computed analytically from IIR coefficients.
    // We approximate by asking EQProcessor for each band's parameters and
    // recomputing the design equations.  This avoids needing to expose the
    // private EQChain from EQProcessor.

    double totalMag = 1.0;
    const double sampleRate = 44100.0;  // nominal; close enough for display

    for (int i = 0; i < EQProcessor::NumBands; ++i)
    {
        const EQBand band = eqProcessor.getBand (i);
        if (!band.enabled)
            continue;

        using CoeffPtr = juce::dsp::IIR::Coefficients<float>::Ptr;
        CoeffPtr coeffs;

        const float f = band.frequencyHz;
        const float g = band.gainDB
                        + (withDynamics ? eqProcessor.getDynamicGainReductionDb (i) : 0.0f);
        const float q = band.Q;

        switch (band.type)
        {
            case EQBandType::Peak:
                coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                    sampleRate, (double)f, (double)q, juce::Decibels::decibelsToGain ((double)g));
                break;
            case EQBandType::LowShelf:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
                    sampleRate, (double)f, (double)q, juce::Decibels::decibelsToGain ((double)g));
                break;
            case EQBandType::HighShelf:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                    sampleRate, (double)f, (double)q, juce::Decibels::decibelsToGain ((double)g));
                break;
            case EQBandType::LowPass:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (
                    sampleRate, (double)f, (double)q);
                break;
            case EQBandType::HighPass:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (
                    sampleRate, (double)f, (double)q);
                break;
            case EQBandType::Notch:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeNotch (
                    sampleRate, (double)f, (double)q);
                break;
        }

        if (coeffs != nullptr)
            totalMag *= coeffs->getMagnitudeForFrequency ((double)hz, sampleRate);
    }

    return juce::Decibels::gainToDecibels ((float)totalMag);
}

//==============================================================================
// Handle positions

juce::Point<float> EQComponent::bandHandlePosition (int bandIndex) const
{
    const EQBand band = eqProcessor.getBand (bandIndex);
    return { xForFrequency (band.frequencyHz), yForGainDB (band.gainDB) };
}

int EQComponent::hitTestBandHandle (juce::Point<float> pos) const
{
    // Return the NEAREST band handle within the hit radius.
    //
    // Gemutete Baender zaehlen MIT (F6, User-Befund 2026-08-15): frueher fielen
    // sie hier heraus und waren damit unanfassbar — wer ein Band stummschaltete,
    // konnte es weder verschieben noch wiederfinden.
    // Previously returned the first match (by index), which meant clicking near
    // a higher-index band always picked a lower-index band at the same y position.
    int   nearestIndex = -1;
    float nearestDist  = kHandleRadius + 2.0f;  // max hit distance

    for (int i = 0; i < EQProcessor::NumBands; ++i)
    {
        const float d = bandHandlePosition (i).getDistanceFrom (pos);
        if (d <= nearestDist)
        {
            nearestDist  = d;
            nearestIndex = i;
        }
    }
    return nearestIndex;
}

//==============================================================================
// Drawing

void EQComponent::drawGrid (juce::Graphics& g) const
{
    // Frequency grid lines at each decade and mid-decade point
    static const float kFreqMarkers[] = {
        20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
        1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f
    };

    g.setColour (juce::Colour (0xff333333));
    for (auto freq : kFreqMarkers)
    {
        const float x = xForFrequency (freq);
        g.drawVerticalLine ((int)x, 0.0f, (float)getHeight());
    }

    // Frequency labels
    g.setColour (juce::Colour (0xff666666));
    g.setFont (juce::FontOptions (9.0f));
    for (auto freq : kFreqMarkers)
    {
        const float x = xForFrequency (freq);
        juce::String label;
        if (freq >= 1000.0f) label = juce::String ((int)(freq / 1000.0f)) + "k";
        else label = juce::String ((int)freq);
        g.drawText (label, (int)x - 15, getHeight() - 14, 30, 12,
                    juce::Justification::centred);
    }

    // dB grid lines at 0, ±3, ±6, ±12
    static const float kDBMarkers[] = { -12.0f, -6.0f, -3.0f, 0.0f, 3.0f, 6.0f, 12.0f };
    for (auto dB : kDBMarkers)
    {
        const float y = yForGainDB (dB);
        if (dB == 0.0f)
        {
            g.setColour (juce::Colour (0xff555555));
            g.drawHorizontalLine ((int)y, 0.0f, (float)getWidth());
        }
        else
        {
            g.setColour (juce::Colour (0xff2d2d2d));
            g.drawHorizontalLine ((int)y, 0.0f, (float)getWidth());
        }
    }

    // dB labels
    g.setColour (juce::Colour (0xff555555));
    g.setFont (juce::FontOptions (9.0f));
    for (auto dB : kDBMarkers)
    {
        const float y = yForGainDB (dB);
        const juce::String label = (dB > 0 ? "+" : "") + juce::String ((int)dB) + " dB";
        g.drawText (label, 2, (int)y - 6, 36, 12, juce::Justification::centredLeft);
    }
}

void EQComponent::drawDynamicCurve (juce::Graphics& g) const
{
    if (! anyDynamicsActive())
        return;

    // Die bewegte Kurve: dieselbe Rechnung, aber mit der Absenkung, die die
    // Regelung GERADE anlegt. In Pro-Q und F6 ist genau das der „dynamische
    // Ausschlag" — man sieht, von wo nach wo das Band zieht, und kann die
    // Schwelle danach setzen statt sie zu raten.
    juce::Path dyn;
    bool started = false;

    for (int px = 0; px < getWidth(); ++px)
    {
        const float y = yForGainDB (getDynamicMagnitudeAtFrequency (frequencyAtX ((float) px)));
        if (! started) { dyn.startNewSubPath ((float) px, y); started = true; }
        else            dyn.lineTo ((float) px, y);
    }

    if (! started)
        return;

    // Bernstein/Amber — im EQ sonst nicht belegt (die statische Kurve ist blau,
    // das Spektrum gruen), und deutlich genug, um die Bewegung zu verfolgen.
    static const juce::Colour kDynColour (0xffffa028);

    // Flaeche ZWISCHEN statischer und dynamischer Kurve: sie zeigt den Betrag
    // der Regelung, nicht nur ihre Lage.
    juce::Path band = dyn;
    for (int px = getWidth() - 1; px >= 0; --px)
        band.lineTo ((float) px, yForGainDB (getMagnitudeAtFrequency (frequencyAtX ((float) px))));
    band.closeSubPath();

    g.setColour (kDynColour.withAlpha (0.22f));
    g.fillPath (band);

    g.setColour (kDynColour);
    g.strokePath (dyn, juce::PathStrokeType (2.0f));

    // Zahlenwert am Band: die Kurve zeigt die Bewegung, die Zahl macht sie
    // ablesbar (F6 zeigt ebenfalls den Wert).
    for (int i = 0; i < EQProcessor::NumBands; ++i)
    {
        if (! eqProcessor.isDynamicsActive (i))
            continue;

        const float gr = eqProcessor.getDynamicGainReductionDb (i);
        if (gr > -0.1f)
            continue;

        const auto pos = bandHandlePosition (i);
        g.setColour (kDynColour);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::String (gr, 1) + " dB",
                    juce::Rectangle<float> (pos.x - 30.0f, pos.y + kHandleRadius + 2.0f, 60.0f, 12.0f),
                    juce::Justification::centred, false);
    }
}

void EQComponent::drawEQCurve (juce::Graphics& g) const
{
    // Sample the combined EQ response at each pixel column
    juce::Path curvePath;
    bool pathStarted = false;

    for (int px = 0; px < getWidth(); ++px)
    {
        const float hz = frequencyAtX ((float)px);
        const float dB = getMagnitudeAtFrequency (hz);
        const float y  = yForGainDB (dB);

        if (!pathStarted)
        {
            curvePath.startNewSubPath ((float)px, y);
            pathStarted = true;
        }
        else
        {
            curvePath.lineTo ((float)px, y);
        }
    }

    if (pathStarted)
    {
        // Filled area under the curve (subtle highlight)
        juce::Path filled = curvePath;
        filled.lineTo ((float)getWidth(), yForGainDB (0.0f));
        filled.lineTo (0.0f, yForGainDB (0.0f));
        filled.closeSubPath();
        g.setColour (juce::Colour (0x180088ff));
        g.fillPath (filled);

        // Curve line
        g.setColour (juce::Colour (0xdd88ccff));
        g.strokePath (curvePath, juce::PathStrokeType (1.5f));
    }
}

void EQComponent::drawBandHandles (juce::Graphics& g) const
{
    // Band colours (one per band — 8 bands)
    static const juce::Colour kBandColours[EQProcessor::NumBands] = {
        juce::Colour (0xff4488ff),  // Band 0: blue
        juce::Colour (0xff44ccff),  // Band 1: cyan-blue
        juce::Colour (0xff44ff88),  // Band 2: green
        juce::Colour (0xffaaff44),  // Band 3: yellow-green
        juce::Colour (0xffffcc44),  // Band 4: yellow
        juce::Colour (0xffffaa44),  // Band 5: orange
        juce::Colour (0xffff6644),  // Band 6: orange-red
        juce::Colour (0xffff4488),  // Band 7: red-pink
    };

    for (int i = 0; i < EQProcessor::NumBands; ++i)
    {
        const EQBand band = eqProcessor.getBand (i);

        // F6 (User-Befund 2026-08-15): ein gemutetes Band VERSCHWAND komplett —
        // man sah nicht mehr, wo man die Frequenz gesetzt hatte, und konnte den
        // Griff auch nicht mehr anfassen. In jedem ueblichen EQ wird er in dieser
        // Lage nur DURCHSICHTIG. Genau so jetzt auch hier: Position und Farbe
        // bleiben ablesbar, der Zustand ist an der Transparenz und am
        // durchgestrichenen Ring zu erkennen.
        const bool muted = ! band.enabled;
        const float alpha = muted ? 0.28f : 1.0f;

        const juce::Point<float> pos = bandHandlePosition (i);
        const bool isDragging = bandHandles[i].isDragging;

        // Outer glow when dragging
        if (isDragging)
        {
            g.setColour (kBandColours[i].withAlpha (0.35f * alpha));
            g.fillEllipse (pos.x - kHandleRadius - 4.0f,
                           pos.y - kHandleRadius - 4.0f,
                           (kHandleRadius + 4.0f) * 2.0f,
                           (kHandleRadius + 4.0f) * 2.0f);
        }

        // Handle fill
        g.setColour (isDragging ? kBandColours[i].withAlpha (alpha)
                                : kBandColours[i].withAlpha (0.8f * alpha));
        g.fillEllipse (pos.x - kHandleRadius,
                       pos.y - kHandleRadius,
                       kHandleRadius * 2.0f,
                       kHandleRadius * 2.0f);

        // Handle outline
        g.setColour (juce::Colours::white.withAlpha (0.6f * alpha));
        g.drawEllipse (pos.x - kHandleRadius,
                       pos.y - kHandleRadius,
                       kHandleRadius * 2.0f,
                       kHandleRadius * 2.0f, 1.0f);

        // Band index label inside handle
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.setFont (juce::FontOptions (9.0f));
        g.drawText (juce::String (i + 1),
                    (int)(pos.x - kHandleRadius),
                    (int)(pos.y - kHandleRadius),
                    (int)(kHandleRadius * 2.0f),
                    (int)(kHandleRadius * 2.0f),
                    juce::Justification::centred);

        // Durchgestrichen = gemutet. Die Transparenz allein koennte man fuer
        // "weit weg" oder "nicht ausgewaehlt" halten; der Strich ist eindeutig.
        if (muted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawLine (pos.x - kHandleRadius, pos.y + kHandleRadius,
                        pos.x + kHandleRadius, pos.y - kHandleRadius, 1.4f);
        }
    }
}

//==============================================================================
// Paint

void EQComponent::paint (juce::Graphics& g)
{
    // Background (SpectrumAnalyzer already draws as a child behind us,
    // but we paint the dark backdrop here for the non-spectrum areas)
    g.fillAll (juce::Colour (0xff1a1a1a));

    drawGrid (g);
    drawEQCurve (g);
    drawDynamicCurve (g);   // F4: bewegte Kurve ueber der statischen
    drawBandHandles (g);
}

//==============================================================================
// Mouse interaction

void EQComponent::mouseDown (const juce::MouseEvent& e)
{
    // Right-click on a band handle: show type/slope/enable menu
    if (e.mods.isRightButtonDown())
    {
        const int bandIdx = hitTestBandHandle (e.position);
        if (bandIdx >= 0)
        {
            EQBand b = eqProcessor.getBand (bandIdx);

            juce::PopupMenu menu;
            menu.addSectionHeader ("Band " + juce::String (bandIdx + 1) + " Type");
            menu.addItem (1, "Bell (Peak)",         true, b.type == EQBandType::Peak);
            menu.addItem (2, "Low Shelf",            true, b.type == EQBandType::LowShelf);
            menu.addItem (3, "High Shelf",           true, b.type == EQBandType::HighShelf);
            // ACHTUNG, hier stand ein echter Fehler (gefunden 2026-08-15):
            // "Low Cut" war auf EQBandType::LowPass gelegt — das ist ein
            // TIEFPASS und nimmt die HOEHEN weg. Wer Trittschall wegschneiden
            // wollte, verlor stattdessen die Brillanz. Ein Low Cut ist ein
            // HOCHPASS (HighPass), ein High Cut ein Tiefpass (LowPass).
            // Die DSP war immer richtig, nur die Beschriftung nicht.
            menu.addItem (4, "Low Cut  12 dB/oct",   true, b.type == EQBandType::HighPass && b.slopeOrder == 2);
            menu.addItem (5, "Low Cut  24 dB/oct",   true, b.type == EQBandType::HighPass && b.slopeOrder == 4);
            menu.addItem (6, "High Cut 12 dB/oct",   true, b.type == EQBandType::LowPass  && b.slopeOrder == 2);
            menu.addItem (7, "High Cut 24 dB/oct",   true, b.type == EQBandType::LowPass  && b.slopeOrder == 4);
            menu.addItem (8, "Notch",                true, b.type == EQBandType::Notch);
            menu.addSeparator();
            menu.addItem (9, b.enabled ? "Disable Band" : "Enable Band");

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                [this, bandIdx] (int result)
                {
                    if (result == 0)
                        return;

                    EQBand updated = eqProcessor.getBand (bandIdx);

                    switch (result)
                    {
                        case 1: updated.type = EQBandType::Peak;      updated.slopeOrder = 2; break;
                        case 2: updated.type = EQBandType::LowShelf;  updated.slopeOrder = 2; break;
                        case 3: updated.type = EQBandType::HighShelf; updated.slopeOrder = 2; break;
                        case 4: updated.type = EQBandType::HighPass;  updated.slopeOrder = 2; break;   // Low Cut
                        case 5: updated.type = EQBandType::HighPass;  updated.slopeOrder = 4; break;   // Low Cut
                        case 6: updated.type = EQBandType::LowPass;   updated.slopeOrder = 2; break;   // High Cut
                        case 7: updated.type = EQBandType::LowPass;   updated.slopeOrder = 4; break;   // High Cut
                        case 8: updated.type = EQBandType::Notch;     updated.slopeOrder = 2; break;
                        case 9: updated.enabled = !updated.enabled;                            break;
                        default: return;
                    }

                    eqProcessor.setBand (bandIdx, updated);
                    repaint();
                });
        }
        return;
    }

    draggingBandIndex = hitTestBandHandle (e.position);
    if (draggingBandIndex >= 0)
    {
        // Die Band-Leiste des Editors folgt dem angefassten Band, damit Solo
        // und Mute ohne Umweg ueber die Bandwahl erreichbar sind.
        if (onBandSelected != nullptr)
            onBandSelected (draggingBandIndex);

        dragStartPos  = e.position;
        dragStartBand = eqProcessor.getBand (draggingBandIndex);
        bandHandles[draggingBandIndex].isDragging = true;
        repaint();
    }
}

void EQComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingBandIndex < 0)
        return;

    const float dx = e.position.x - dragStartPos.x;
    const float dy = e.position.y - dragStartPos.y;

    EQBand updatedBand = dragStartBand;

    if (e.mods.isCtrlDown())
    {
        // Ctrl + drag vertical: change Q
        // Upward drag narrows Q (higher Q = narrower), downward widens
        const float qDelta = -dy * 0.05f;
        updatedBand.Q = juce::jlimit (0.1f, 10.0f, dragStartBand.Q + qDelta);
    }
    else
    {
        // Horizontal: change frequency
        const float newX = juce::jlimit (0.0f, (float)getWidth(),
                                         dragStartPos.x + dx);
        updatedBand.frequencyHz = frequencyAtX (newX);

        // Vertical: change gain
        const float newY = juce::jlimit (0.0f, (float)getHeight(),
                                         dragStartPos.y + dy);
        updatedBand.gainDB = gainDBAtY (newY);
    }

    eqProcessor.setBand (draggingBandIndex, updatedBand);

    // Update drag readout label
    {
        const EQBand& b = eqProcessor.getBand (draggingBandIndex);
        const juce::String freqText = b.frequencyHz >= 1000.0f
            ? juce::String (b.frequencyHz / 1000.0f, 2) + " kHz"
            : juce::String ((int)b.frequencyHz) + " Hz";
        juce::String info;
        if (e.mods.isCtrlDown())
            info = freqText + "  /  Q " + juce::String (b.Q, 2);
        else
            info = freqText + "  /  " + juce::String (b.gainDB, 1) + " dB"
                 + "  /  Q " + juce::String (b.Q, 2);

        dragInfoLabel.setText (info, juce::dontSendNotification);
        const auto pos = bandHandlePosition (draggingBandIndex);
        const int labelW = 190;
        const int labelH = 18;
        int labelX = (int)pos.x - labelW / 2;
        int labelY = (int)pos.y - 28;
        labelX = juce::jlimit (0, juce::jmax (0, getWidth() - labelW), labelX);
        labelY = juce::jlimit (0, juce::jmax (0, getHeight() - labelH), labelY);
        dragInfoLabel.setBounds (labelX, labelY, labelW, labelH);
        dragInfoLabel.setVisible (true);
    }

    repaint();
}

void EQComponent::mouseUp (const juce::MouseEvent& /*e*/)
{
    if (draggingBandIndex >= 0)
    {
        bandHandles[draggingBandIndex].isDragging = false;
        draggingBandIndex = -1;
        dragInfoLabel.setVisible (false);
        repaint();
    }
}

void EQComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int bandIndex = hitTestBandHandle (e.position);
    if (bandIndex < 0)
        return;

    // Double-click: reset band to flat (0dB, default Q)
    EQBand resetBand = eqProcessor.getBand (bandIndex);
    resetBand.gainDB = 0.0f;
    resetBand.Q = 0.707f;
    eqProcessor.setBand (bandIndex, resetBand);
    repaint();
}

void EQComponent::mouseWheelMove (const juce::MouseEvent& e,
                                   const juce::MouseWheelDetails& wheel)
{
    // Scroll over a band handle adjusts its Q (standard in Logic Pro / Studio One).
    // Scroll up = higher Q (narrower), scroll down = lower Q (wider).
    const int bandIndex = hitTestBandHandle (e.position);
    if (bandIndex < 0)
        return;

    EQBand band = eqProcessor.getBand (bandIndex);
    band.Q = juce::jlimit (0.1f, 10.0f, band.Q + wheel.deltaY * 0.5f);
    eqProcessor.setBand (bandIndex, band);
    repaint();
}
