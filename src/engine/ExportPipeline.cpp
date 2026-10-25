#include "ExportPipeline.h"
#include "AizanaUIBehaviour.h"
#include "Mp3Encoder.h"
#include <atomic>
#include <cmath>

namespace
{
// Rendering pumps messages: reject nested exports even from another dialog.
bool exportInProgress = false;
juce::String validate(const ExportOptions& o)
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
        return "Export muss auf dem Hauptthread gestartet werden.";
    if (o.sampleRate != 44100 && o.sampleRate != 48000 && o.sampleRate != 96000)
        return "Ungueltige Samplerate.";
    if (o.format == ExportFormat::MP3)
    {
        if (o.sampleRate == 96000) return "MP3 unterstuetzt nur 44,1 oder 48 kHz.";
        if (o.mp3BitrateKbps != 128 && o.mp3BitrateKbps != 192
            && o.mp3BitrateKbps != 256 && o.mp3BitrateKbps != 320)
            return "Ungueltige MP3-Bitrate.";
    }
    else if (o.bitDepth != 16 && o.bitDepth != 24 && o.bitDepth != 32)
        return "Ungueltige Bittiefe.";
    if (o.shouldCancel && o.shouldCancel()) return "Export abgebrochen.";
    return {};
}
}

ExportPipeline::ExportPipeline() { formatManager.registerBasicFormats(); }

juce::String ExportPipeline::extensionFor(ExportFormat fmt)
{
    switch (fmt)
    {
        case ExportFormat::WAV: return "wav";
        case ExportFormat::AIFF: return "aif";
        case ExportFormat::MP3: return "mp3";
    }
    return {};
}

juce::AudioFormat* ExportPipeline::formatFor(ExportFormat fmt)
{
    return formatManager.findFormatForFileExtension(fmt == ExportFormat::AIFF ? "aif" : "wav");
}

tracktion::core::TimeRange ExportPipeline::editTimeRange(te::Edit& edit)
{
    double end = 0;
    for (auto* track : te::getAudioTracks(edit))
        for (auto* clip : track->getClips())
            end = std::max(end, clip->getPosition().getEnd().inSeconds());
    // Include effect decay in the actual render interval, so the WAV length
    // check below remains exact (Renderer endAllowance may stop at silence).
    // Summing declared tails is conservative for parallel/serial bus chains.
    double tail = 0.0;
    auto addTail = [&tail] (te::Plugin* plugin)
    {
        if (! plugin->isEnabled()) return;
        if (auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin))
            ext->initialiseFully();
        const double seconds = plugin->getTailLength();
        if (seconds > 0.0)
            tail = std::min (60.0, tail + (std::isfinite(seconds) ? seconds : 60.0));
    };
    for (auto* track : te::getAllTracks(edit))
        for (auto* plugin : track->getAllPlugins()) addTail(plugin);
    for (auto* plugin : edit.getMasterPluginList()) addTail(plugin);
    return {tracktion::core::TimePosition::fromSeconds(0),
            tracktion::core::TimePosition::fromSeconds((end > 0 ? end : 1) + tail)};
}

juce::String ExportPipeline::renderOne(te::Renderer::Parameters& params,
                                      const ExportOptions& options, const juce::File& destination)
{
    if (destination.isDirectory()) return "Das Exportziel ist ein Ordner.";
    if (destination.getParentDirectory().createDirectory().failed())
        return "Exportordner konnte nicht erstellt werden.";
    // Sibling temporary files permit atomic replacement on the same filesystem.
    juce::TemporaryFile rendered(destination.getSiblingFile(
        ".aizana-render-" + juce::Uuid().toString() + (options.format == ExportFormat::AIFF ? ".aif" : ".wav")));
    juce::TemporaryFile encoded(destination);
    params.destFile = rendered.getFile();
    params.audioFormat = formatFor(options.format);
    params.bitDepth = options.format == ExportFormat::MP3 ? 32 : options.bitDepth;
    params.sampleRateForAudio = options.sampleRate;
    params.blockSizeForAudio = 512;
    params.realTimeRender = false;
    params.canRenderInMono = false;
    params.usePlugins = true;
    params.checkNodesForAudio = false;
    const juce::ScopedValueSetter<std::function<bool()>> cancelScope(
        AizanaUIBehaviour::renderShouldCancel, options.shouldCancel);
    const juce::ScopedValueSetter<bool> abortScope(AizanaUIBehaviour::renderWasAborted, false);
    const auto result = te::Renderer::renderToFile("Aizana Export", params);
    if (AizanaUIBehaviour::renderWasAborted || (options.shouldCancel && options.shouldCancel()))
        return "Export abgebrochen oder Render-Zeitlimit erreicht.";
    if (result != params.destFile || !result.existsAsFile())
        return "Render fehlgeschlagen: keine vollstaendige Audiodatei.";
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(result));
    const auto expectedSamples = std::llround(params.time.getLength().inSeconds() * options.sampleRate);
    if (!reader || reader->numChannels != 2 || reader->sampleRate != options.sampleRate
        || std::abs(reader->lengthInSamples - expectedSamples) > 1)
        return "Render fehlgeschlagen: unvollstaendige oder ungueltige Audiodaten.";
    reader.reset();

    if (options.format == ExportFormat::MP3)
    {
        struct EncoderThread : juce::Thread
        {
            EncoderThread(const juce::File& in, const juce::File& out, int rate)
                : Thread("MP3 Export"), input(in), output(out), bitrate(rate) {}
            void run() override
            {
                error = Mp3Encoder::encode(input, output, bitrate, [this] { return cancelled.load(); });
            }
            juce::File input, output;
            int bitrate;
            std::atomic<bool> cancelled {false};
            juce::String error;
        } worker(result, encoded.getFile(), options.mp3BitrateKbps);
        if (!worker.startThread()) return "MP3-Encoder konnte nicht gestartet werden.";
        while (worker.isThreadRunning())
        {
            if (options.shouldCancel && options.shouldCancel()) worker.cancelled.store(true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        }
        worker.waitForThreadToExit(-1);
        if (worker.error.isNotEmpty()) return worker.error;
        if (options.shouldCancel && options.shouldCancel()) return "Export abgebrochen.";
    }
    else if (!result.moveFileTo(encoded.getFile()))
        return "Exportdatei konnte nicht vorbereitet werden.";
    if (!encoded.overwriteTargetFileWithTemporary())
        return "Exportdatei konnte nicht gespeichert werden. Vorhandenes Ziel bleibt erhalten.";
    return {};
}

juce::String ExportPipeline::exportMixdown(te::Edit& edit, const ExportOptions& options)
{
    if (auto error = validate(options); error.isNotEmpty()) return error;
    if (exportInProgress) return "Ein Export laeuft bereits.";
    const juce::ScopedValueSetter<bool> busy(exportInProgress, true);
    te::Renderer::Parameters params(edit);
    params.time = editTimeRange(edit);
    auto tracks = te::getAllTracks(edit);
    for (int i = 0; i < tracks.size(); ++i) params.tracksToDo.setBit(i);
    params.useMasterPlugins = true;
    if (onProgress) onProgress(0);
    // Detach the live graph so plugins are processed exclusively by the renderer.
    const te::Edit::ScopedRenderStatus renderStatus(edit, true);
    auto error = renderOne(params, options, options.outputFile);
    if (error.isEmpty() && onProgress) onProgress(1);
    return error;
}

juce::String ExportPipeline::exportStems(te::Edit& edit, const ExportOptions& options)
{
    if (auto error = validate(options); error.isNotEmpty()) return error;
    if (exportInProgress) return "Ein Export laeuft bereits.";
    const juce::ScopedValueSetter<bool> busy(exportInProgress, true);
    if (options.outputFile.createDirectory().failed()) return "Stem-Ordner konnte nicht erstellt werden.";
    if (te::getAudioTracks(edit).isEmpty()) return "Keine Audiospuren zum Exportieren.";
    const te::Edit::ScopedRenderStatus renderStatus(edit, true);
    edit.flushState();
    // Render a private snapshot: taking a source stem before its downstream bus
    // must not modify the live session routing, undo history, or dirty state.
    const auto snapshotState = edit.state.createCopy();
    auto tracks = te::getAudioTracks(edit);
    const auto time = editTimeRange(edit);
    juce::StringArray usedNames;
    int index = 0;
    for (auto* originalTrack : tracks)
    {
        if (options.shouldCancel && options.shouldCancel()) return "Export abgebrochen.";
        // A fresh snapshot per stem also avoids asynchronous routing-cache
        // updates from one stem affecting the next bus render.
        te::Edit::Options snapshotOptions { edit.engine, snapshotState.createCopy(), edit.getProjectItemID() };
        snapshotOptions.role = te::Edit::forRendering;
        snapshotOptions.editFileRetriever = edit.editFileRetriever;
        snapshotOptions.filePathResolver = edit.filePathResolver;
        auto snapshot = te::Edit::createEdit(snapshotOptions);
        if (! snapshot) return "Export-Snapshot konnte nicht erstellt werden.";
        auto* track = dynamic_cast<te::AudioTrack*>(te::getAllTracks(*snapshot)[originalTrack->getIndexInEditTrackList()]);
        if (! track) return "Exportspur konnte nicht gefunden werden.";
        auto name = juce::File::createLegalFileName(track->getName()).trim();
        if (name.isEmpty() || name == "." || name == "..") name = "Spur";
        auto unique = name;
        for (int suffix = 2; usedNames.contains(unique, true); ++suffix)
            unique = name + "_" + juce::String(suffix);
        usedNames.add(unique);
        // Source stems include their own inserts/fader. Bus stems include
        // their upstream channels. Downstream buses/master are separate stems.
        auto& output = track->getOutput();
        output.setOutputToDefaultDevice(false);
        te::Renderer::Parameters params(*snapshot);
        params.time = time;
        params.tracksToDo.setBit(track->getIndexInEditTrackList());
        if (onProgress) onProgress(static_cast<float>(index) / tracks.size());
        auto error = renderOne(params, options, options.outputFile.getChildFile(unique + "." + extensionFor(options.format)));
        if (error.isNotEmpty()) return unique + ": " + error;
        ++index;
    }
    if (onProgress) onProgress(1);
    return {};
}
