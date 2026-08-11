// test_export_wav.cpp — EXP-01: Stereo-Mixdown-Export; EXP-04: Format/Samplerate
//
// Die ersten drei Faelle sind seit Phase 1 Stubs geblieben (die Renderer-Pfade
// waren headless nicht fahrbar, bis JUCE_MODAL_LOOPS_PERMITTED auch fuer
// engine_tests gesetzt wurde). Neu dazu 2026-08-11 ein Fall, der den
// Auslieferungspfad an genau der Stelle absichert, an der er nachweislich
// falsch lief:
//
//   Waehrend eines Offline-Renders haengt der Edit weiter am Audio-Geraet, und
//   der LIVE-Graph verarbeitet dieselben Plugin-Instanzen weiter. Bei
//   zustandsbehafteten Plugins (DC-Sperren, Kompressor-Huellkurven, Hall-Fahnen)
//   verfaelscht das den Export — er ist weder reproduzierbar noch identisch mit
//   dem, was man gehoert hat. TE meldet die Lage selbst und stellt
//   te::Edit::ScopedRenderStatus bereit; Aizana hat es nur nicht benutzt.
#include <catch2/catch_test_macros.hpp>

#include "engine/EditManager.h"
#include "engine/ClipEditor.h"
#include "engine/ExportPipeline.h"

TEST_CASE("ExportWAV: rendered output matches expected offline render checksum", "[export][EXP-01][stub]") {
    SKIP("Stub — implement when RenderPipeline is available (plan 01-07)");
}

TEST_CASE("ExportWAV: 96kHz/24-bit export produces valid WAV header with correct sample rate", "[export][EXP-04][stub]") {
    SKIP("Stub — implement when RenderPipeline is available (plan 01-07)");
}

TEST_CASE("ExportWAV: 44.1kHz/16-bit and 48kHz/24-bit exports both produce valid WAV files", "[export][EXP-04][stub]") {
    SKIP("Stub — implement when RenderPipeline is available (plan 01-07)");
}

// ---------------------------------------------------------------------------
// Der Export darf nicht laufen, waehrend der Edit am Audio-Geraet haengt.
// Geprueft wird TEs eigene Meldung — sie ist der eindeutige, von TE selbst
// definierte Nachweis der fehlerhaften Lage (tracktion_NodeRenderContext.cpp,
// direkt neben einem jassertfalse). Ohne den Fix schlaegt der Fall fehl
// (verifiziert durch Deaktivieren des Fixes).
// ---------------------------------------------------------------------------
TEST_CASE ("Export: mixdown does not render while attached to the audio device", "[export][EXP-01][reproducible]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_export_attach_test");
    tempDir.deleteRecursively();
    tempDir.createDirectory();

    // Eine Sekunde Sinus als Inhalt — ohne Audio rendert TE nichts Pruefbares.
    const auto srcWav = tempDir.getChildFile ("src.wav");
    {
        juce::WavAudioFormat wav;
        auto stream = srcWav.createOutputStream();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), 44100.0, 1, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        juce::AudioBuffer<float> buf (1, 44100);
        for (int i = 0; i < buf.getNumSamples(); ++i)
            buf.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                          * 440.0 * i / 44100.0));
        REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()));
    }

    auto* track = te::getAudioTracks (*edit)[0];
    REQUIRE (clipEditor.importAudioFile (srcWav, *track, 0.0) != nullptr);

    // Der Edit haengt am Geraet — die Lage, in der ein User exportiert.
    edit->getTransport().ensureContextAllocated();
    REQUIRE (edit->getTransport().isPlayContextActive());

    struct CapturingLogger : juce::Logger
    {
        juce::StringArray lines;
        void logMessage (const juce::String& m) override { lines.add (m); }
    };

    CapturingLogger logger;
    auto* previousLogger = juce::Logger::getCurrentLogger();
    juce::Logger::setCurrentLogger (&logger);

    ExportOptions options;
    options.outputFile = tempDir.getChildFile ("mixdown.wav");
    options.format     = ExportFormat::WAV;
    options.sampleRate = 44100.0;
    options.bitDepth   = 24;

    ExportPipeline pipeline;
    const auto err = pipeline.exportMixdown (*edit, options);

    juce::Logger::setCurrentLogger (previousLogger);

    INFO ("Export-Fehler: " << err);
    INFO ("TE-Meldungen: " << logger.lines.joinIntoString (" | "));

    bool renderedWhileAttached = false;
    for (const auto& line : logger.lines)
        if (line.contains ("Rendering whilst attached to audio device"))
            renderedWhileAttached = true;

    CHECK_FALSE (renderedWhileAttached);

    // Und der Export muss trotzdem eine brauchbare Datei liefern — ein Fix, der
    // den Export stilllegt, waere kein Fix.
    CHECK (err.isEmpty());
    REQUIRE (options.outputFile.existsAsFile());
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (options.outputFile));
        REQUIRE (reader != nullptr);
        juce::AudioBuffer<float> b ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&b, 0, b.getNumSamples(), 0, true, b.getNumChannels() > 1);
        CHECK (b.getMagnitude (0, b.getNumSamples()) > 0.01f);
    }

    // Nach dem Export ist die Session wieder spielbereit (ScopedRenderStatus
    // legt den Kontext wieder an) — sonst waere nach jedem Export Stille.
    CHECK (edit->getTransport().isPlayContextActive());

    tempDir.deleteRecursively();
}
