#pragma once

#include <tracktion_engine/tracktion_engine.h>

/**
 * CloudesTrackColours — shared track-colour resolution for the colour-coded
 * arrangement (Sketch 005 P1, closes GAP-27).
 *
 * Tracktion's per-track colour (te::Track::getColour, persisted in the edit
 * ValueTree) defaults to a near-black transparent colour. To make rows read as
 * colour-coded out of the box — without mutating session state — an uncoloured
 * track falls back to a stable palette colour derived from its position among
 * the audio tracks. Once the user picks a colour it overrides the default.
 *
 * Used by TrackHeader (colour strip), TrackRow (row wash) and ClipComponent
 * (clip body) so all three always agree on a track's colour.
 */
namespace CloudesTrackColours
{
    namespace te = tracktion::engine;

    /** A distinct, pleasant palette (Studio One-ish). Index wraps. */
    inline juce::Colour palette (int index) noexcept
    {
        static const juce::Colour colours[] = {
            juce::Colour (0xff4a90d9),  // blue
            juce::Colour (0xff5cb85c),  // green
            juce::Colour (0xffd9534f),  // red
            juce::Colour (0xfff0ad4e),  // orange
            juce::Colour (0xff9b59b6),  // purple
            juce::Colour (0xff1abc9c),  // teal
            juce::Colour (0xffe67e22),  // pumpkin
            juce::Colour (0xffe84393),  // pink
            juce::Colour (0xff95a5a6),  // grey
            juce::Colour (0xfff1c40f),  // yellow
            juce::Colour (0xff48a9d8),  // sky
            juce::Colour (0xff2ecc71),  // emerald
        };
        constexpr int n = (int) (sizeof (colours) / sizeof (colours[0]));
        return colours[((index % n) + n) % n];
    }

    /** Number of palette swatches. */
    inline int paletteSize() noexcept { return 12; }

    /** Human-readable name for a palette index (German UI). */
    inline juce::String paletteName (int index) noexcept
    {
        static const char* names[] = {
            "Blau", "Gr\xc3\xbcn", "Rot", "Orange", "Lila", "T\xc3\xbcrkis",
            "K\xc3\xbcrbis", "Pink", "Grau", "Gelb", "Himmel", "Smaragd"
        };
        constexpr int n = (int) (sizeof (names) / sizeof (names[0]));
        return juce::String::fromUTF8 (names[((index % n) + n) % n]);
    }

    /** True when the track carries a real, user-set colour (not the near-black default). */
    inline bool hasCustomColour (const te::Track& track) noexcept
    {
        return track.getColour().getBrightness() > 0.05f;
    }

    /** The colour to display for a track: its own colour if set, else a stable
     *  palette default derived from its position among the audio tracks. */
    inline juce::Colour displayColour (te::Track& track)
    {
        if (hasCustomColour (track))
            return track.getColour();

        int idx = 0;
        if (auto* at = dynamic_cast<te::AudioTrack*> (&track))
            idx = juce::jmax (0, te::getAudioTracks (track.edit).indexOf (at));
        return palette (idx);
    }
}
