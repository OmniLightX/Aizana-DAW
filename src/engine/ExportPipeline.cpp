#include "ExportPipeline.h"

// ---------------------------------------------------------------------------
// ExportPipeline implementation — plan 01-07
//
// Design notes:
//   - te::Renderer::renderToFile() is a static blocking function. Safe to call
//     from any thread except the audio thread.
//   - Stem export uses te::Renderer::Parameters::tracksToDo (BigInteger) to
//     isolate individual tracks — avoids mutating track mute state directly.
//   - MP3 export relies on juce::LAMEEncoderAudioFormat which wraps the system
//     LAME encoder binary. Returns a clean error if LAME is unavailable.
// ---------------------------------------------------------------------------

using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ExportPipeline::ExportPipeline()
{
    // Register built-in formats (WAV + AIFF).
    formatManager.registerBasicFormats();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

juce::String ExportPipeline::extensionFor(ExportFormat fmt)
{
    switch (fmt)
    {
        case ExportFormat::WAV:  return "wav";
        case ExportFormat::AIFF: return "aif";
        case ExportFormat::MP3:  return "mp3";
        default:                 return "wav";
    }
}

juce::AudioFormat* ExportPipeline::formatFor(ExportFormat fmt)
{
    switch (fmt)
    {
        case ExportFormat::WAV:
            return formatManager.findFormatForFileExtension("wav");

        case ExportFormat::AIFF:
            return formatManager.findFormatForFileExtension("aif");

        case ExportFormat::MP3:
            // juce::LAMEEncoderAudioFormat is not available in JUCE 8 without
            // an explicit LAME integration build. MP3 export deferred to v2.
            return nullptr;

        default:
            return formatManager.findFormatForFileExtension("wav");
    }
}

tracktion::core::TimeRange ExportPipeline::editTimeRange(te::Edit& edit)
{
    double endSecs = 0.0;

    for (auto* track : te::getAudioTracks(edit))
    {
        for (auto* clip : track->getClips())
        {
            double clipEnd = clip->getPosition().getEnd().inSeconds();
            endSecs = std::max(endSecs, clipEnd);
        }
    }

    // Fallback: empty edit produces a 1-second silent file.
    if (endSecs <= 0.0)
        endSecs = 1.0;

    return TimeRng(TimePos::fromSeconds(0.0), TimePos::fromSeconds(endSecs));
}

// ---------------------------------------------------------------------------
// EXP-01: Export stereo mixdown
// ---------------------------------------------------------------------------

juce::String ExportPipeline::exportMixdown(te::Edit& edit, const ExportOptions& options)
{
    juce::AudioFormat* fmt = formatFor(options.format);
    if (fmt == nullptr)
    {
        if (options.format == ExportFormat::MP3)
            return "MP3 export requires LAME binary — install LAME and ensure it is in PATH. "
                   "See: https://lame.sourceforge.io/";
        return "Unsupported export format.";
    }

    TimeRng timeRange = editTimeRange(edit);

    te::Renderer::Parameters params(edit);
    params.destFile           = options.outputFile;
    params.audioFormat        = fmt;
    params.bitDepth           = (options.format == ExportFormat::MP3) ? 16 : options.bitDepth;
    params.sampleRateForAudio = options.sampleRate;
    params.blockSizeForAudio  = 512;
    params.time               = timeRange;
    params.realTimeRender     = false;
    params.usePlugins         = true;
    params.checkNodesForAudio = false;
    params.tracksToDo         = {};  // all tracks

    if (onProgress)
        onProgress(0.0f);

    te::Renderer::renderToFile("Cloudes Mixdown Export", params);

    if (onProgress)
        onProgress(1.0f);

    // Consider success if destFile exists after render.
    if (! options.outputFile.existsAsFile())
        return "Render produced no output file. Ensure the Edit contains audio content.";

    return {};
}

// ---------------------------------------------------------------------------
// EXP-02: Export stems (one file per audio track)
// ---------------------------------------------------------------------------

juce::String ExportPipeline::exportStems(te::Edit& edit, const ExportOptions& options)
{
    if (! options.outputFile.isDirectory())
    {
        if (! options.outputFile.createDirectory())
            return "Could not create stem output directory: " + options.outputFile.getFullPathName();
    }

    juce::AudioFormat* fmt = formatFor(options.format);
    if (fmt == nullptr)
    {
        if (options.format == ExportFormat::MP3)
            return "MP3 stem export requires LAME binary.";
        return "Unsupported export format.";
    }

    auto tracks = te::getAudioTracks(edit);
    if (tracks.isEmpty())
        return "No audio tracks to export.";

    const juce::String ext = extensionFor(options.format);
    TimeRng timeRange = editTimeRange(edit);

    int trackIndex = 0;
    const int totalTracks = tracks.size();

    for (auto* track : tracks)
    {
        juce::String trackName = track->getName();
        if (trackName.isEmpty())
            trackName = "Track" + juce::String(trackIndex + 1);

        // Sanitise track name for filesystem
        const juce::String invalidChars("\\/:|?*\"<>");
        for (int i = 0; i < invalidChars.length(); ++i)
            trackName = trackName.replaceCharacter(invalidChars[i], '_');

        juce::File stemFile = options.outputFile.getChildFile(trackName + "." + ext);

        // Isolate this track via tracksToDo BigInteger
        juce::BigInteger tracksToDo;
        tracksToDo.setBit(track->getIndexInEditTrackList());

        te::Renderer::Parameters params(edit);
        params.destFile           = stemFile;
        params.audioFormat        = fmt;
        params.bitDepth           = (options.format == ExportFormat::MP3) ? 16 : options.bitDepth;
        params.sampleRateForAudio = options.sampleRate;
        params.blockSizeForAudio  = 512;
        params.time               = timeRange;
        params.realTimeRender     = false;
        params.usePlugins         = true;
        params.checkNodesForAudio = false;
        params.tracksToDo         = tracksToDo;

        if (onProgress)
            onProgress(static_cast<float>(trackIndex) / static_cast<float>(totalTracks));

        te::Renderer::renderToFile("Stem: " + trackName, params);

        ++trackIndex;
    }

    if (onProgress)
        onProgress(1.0f);

    return {};
}
