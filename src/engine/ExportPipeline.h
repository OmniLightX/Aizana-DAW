#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

// ---------------------------------------------------------------------------
// ExportFormat — supported audio container formats (EXP-01, EXP-04)
// ---------------------------------------------------------------------------
enum class ExportFormat
{
    WAV,   // PCM WAV via juce::WavAudioFormat   — 16/24/32-bit
    AIFF,  // AIFF via juce::AiffAudioFormat      — 16/24/32-bit
    MP3    // MP3 via bundled libmp3lame
};

// ---------------------------------------------------------------------------
// ExportOptions — parameters for a single export pass
// ---------------------------------------------------------------------------
struct ExportOptions
{
    /** Output file (for mixdown) or output directory (for stems). */
    juce::File outputFile;

    ExportFormat format     = ExportFormat::WAV;
    double       sampleRate = 44100.0;   // 44100 | 48000 | 96000 (EXP-04)
    int          mp3BitrateKbps = 320;  // 128 | 192 | 256 | 320 CBR
    std::function<bool()> shouldCancel; // polled on the message thread
    int          bitDepth   = 24;        // 16 | 24 | 32 — ignored for MP3
};

// ---------------------------------------------------------------------------
// ExportPipeline — stereo mixdown and per-stem export (EXP-01, EXP-02, EXP-04)
//
// Uses te::Renderer::renderToFile() for offline rendering (blocking).
//
// MESSAGE THREAD ONLY (I-10). Der Kommentar an dieser Stelle behauptete frueher
// das Gegenteil ("call from a background thread") — das war falsch und stand im
// Widerspruch zur einzigen Aufrufstelle (ExportDialog, direkt aus dem
// Button-Klick) wie auch zur Invariante. Zwei Gruende, warum nur der
// Message-Thread geht: te::Renderer delegiert an
// AizanaUIBehaviour::runTaskWithProgressBar (pumpt den Message-Loop), und der
// Export loest ueber te::Edit::ScopedRenderStatus den Playback-Kontext, was TE
// ausdruecklich nur auf dem Message-Thread erlaubt.
//
// Thread model: not thread-safe; one export at a time per instance.
// ---------------------------------------------------------------------------
class ExportPipeline
{
public:
    ExportPipeline();
    ~ExportPipeline() = default;

    // -----------------------------------------------------------------------
    // EXP-01: Export stereo mixdown
    //
    // Renders all tracks into a single stereo file.
    // Returns empty string on success; human-readable error on failure.
    // -----------------------------------------------------------------------
    juce::String exportMixdown(te::Edit& edit, const ExportOptions& options);

    // -----------------------------------------------------------------------
    // EXP-02: Export per-stem files
    //
    // options.outputFile must be a directory.
    // Creates one file per audio track named {TrackName}.{ext}.
    // Returns empty string on success; error string on failure.
    // -----------------------------------------------------------------------
    juce::String exportStems(te::Edit& edit, const ExportOptions& options);

    // -----------------------------------------------------------------------
    // Progress callback (0.0–1.0). Optional — set before calling export methods.
    // -----------------------------------------------------------------------
    std::function<void(float)> onProgress;

private:
    juce::AudioFormatManager formatManager;
    juce::String renderOne(te::Renderer::Parameters&, const ExportOptions&, const juce::File&);

    /** PCM render format; MP3 is encoded from an intermediate WAV. */
    juce::AudioFormat* formatFor(ExportFormat fmt);

    /** File extension string — "wav", "aif", or "mp3". */
    static juce::String extensionFor(ExportFormat fmt);

    /** Determine the edit's time range (start → end of last clip). */
    static tracktion::core::TimeRange editTimeRange(te::Edit& edit);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportPipeline)
};
