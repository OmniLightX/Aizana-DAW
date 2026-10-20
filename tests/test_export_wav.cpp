#include "ScopedTestLogger.h"
// test_export_wav.cpp — EXP-01: Stereo-Mixdown-Export; EXP-04: Format/Samplerate
//
// Die frueheren Platzhalter pruefen jetzt Header, PCM und Wiederholbarkeit.
// Der weitere Regressionstest sichert den Offline-/Live-Graph-Konflikt ab:
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

namespace
{
struct ExportFixture
{
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("aizana-export-contract", {}, false);
    EditManager manager;
    ExportFixture()
    {
        REQUIRE(dir.createDirectory().wasOk());
        manager.createNewEdit();
        // Tracktion initializes the master at -3 dB; this reference uses unity gain.
        auto& edit=*manager.getEdit();
        auto& um=edit.getUndoManager(); um.beginNewTransaction("Reference unity gain");
        edit.getMasterVolumePlugin()->volume.setValue(te::decibelsToVolumeFaderPosition(0.0f), &um);
        edit.getMasterVolumePlugin()->setVolumeDb(0.0f);

        auto input = dir.getChildFile("input.wav");
        auto stream = input.createOutputStream();
        REQUIRE(stream != nullptr);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(),44100,2,24,{},0));
        REQUIRE(writer != nullptr); stream.release();
        juce::AudioBuffer<float> audio(2,44100);
        for (int c=0;c<2;++c)
            for (int i=0;i<44100;++i)
                audio.setSample(c,i,0.2f*std::sin(2.0*juce::MathConstants<double>::pi*(c==0?440:660)*i/44100.0));
        REQUIRE(writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()));
        writer.reset();
        ClipEditor editor(manager);
        REQUIRE(editor.importAudioFile(input,*te::getAudioTracks(*manager.getEdit())[0],0.0) != nullptr);
    }
    ~ExportFixture() { dir.deleteRecursively(); }
    juce::AudioBuffer<float> render(double rate,int bits,const char* filename)
    {
        ExportOptions options;
        options.outputFile=dir.getChildFile(filename);options.sampleRate=rate;options.bitDepth=bits;
        ExportPipeline pipeline;
        REQUIRE(pipeline.exportMixdown(*manager.getEdit(),options).isEmpty());
        juce::AudioFormatManager formats;formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(options.outputFile));
        REQUIRE(reader != nullptr);
        CHECK(reader->sampleRate==rate);CHECK(reader->bitsPerSample==bits);REQUIRE(reader->numChannels==2);
        CHECK(std::abs(reader->lengthInSamples-(juce::int64)rate)<=1);
        juce::AudioBuffer<float> result(2,(int)reader->lengthInSamples);
        REQUIRE(reader->read(&result,0,result.getNumSamples(),0,true,true));
        // Independent known stereo signal. The pinned TE WaveNode uses JUCE's
        // Lagrange interpolator without source-latency compensation: two SOURCE
        // samples (45.35 us at 44.1 kHz), also at unity ratio. Verify this fixed
        // response, not a best-fit shift which could hide a new alignment bug.
        // Edges are omitted because clip start/end fades affect them.
        for(int c=0;c<2;++c)
        {
            double error=0.0;
            for(int i=(int)(rate*0.1);i<(int)(rate*0.9);++i)
            {
                auto expected=0.2*std::sin(2.0*juce::MathConstants<double>::pi*(c==0?440:660)*(i/rate - 2.0/44100.0));
                error=std::max(error,std::abs(result.getSample(c,i)-expected));
            }
            INFO("channel="<<c<<" rate="<<rate<<" bits="<<bits<<" max error="<<error);
            // WaveNode rounds each resampled block to whole source frames.
            // Bound that quantisation by half a source sample at this frequency;
            // unity-rate output has no such block-rounding allowance.
            const double phaseBound = rate == 44100.0 ? 0.0
                : 0.2 * 2.0 * juce::MathConstants<double>::pi * (c == 0 ? 440 : 660) / 44100.0 * 0.5;
            CHECK(error < phaseBound + 2.0 / 32768.0);
        }
        return result;
    }
};
}
TEST_CASE("ExportWAV: repeatable PCM matches the independent stereo reference", "[export][EXP-01]")
{
    ExportFixture f;
    auto a=f.render(44100,24,"first.wav");auto b=f.render(44100,24,"second.wav");
    REQUIRE(a.getNumSamples()==b.getNumSamples());
    for(int c=0;c<2;++c)
        CHECK(std::memcmp(a.getReadPointer(c),b.getReadPointer(c),a.getNumSamples()*sizeof(float))==0);
}
TEST_CASE("ExportWAV: 96kHz/24-bit output has valid header and stereo signal", "[export][EXP-04]")
{
    ExportFixture f;f.render(96000,24,"96k.wav");
}
TEST_CASE("ExportWAV: 44.1kHz/16-bit and 48kHz/24-bit retain the stereo signal", "[export][EXP-04]")
{
    ExportFixture f;f.render(44100,16,"44k.wav");f.render(48000,24,"48k.wav");
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

    ScopedTestLogger logger;

    ExportOptions options;
    options.outputFile = tempDir.getChildFile ("mixdown.wav");
    options.format     = ExportFormat::WAV;
    options.sampleRate = 44100.0;
    options.bitDepth   = 24;

    ExportPipeline pipeline;
    const auto err = pipeline.exportMixdown (*edit, options);


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
