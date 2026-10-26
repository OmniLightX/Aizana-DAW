#include <catch2/matchers/catch_matchers_floating_point.hpp>
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

TEST_CASE("ExportMP3: produces a decodable stereo MP3", "[export][mp3]")
{
    ExportFixture f;
    ExportOptions options;
    options.format = ExportFormat::MP3;
    options.outputFile = f.dir.getChildFile("mix.mp3");
    ExportPipeline pipeline;
    auto error = pipeline.exportMixdown(*f.manager.getEdit(), options);
    INFO(error);
    REQUIRE(error.isEmpty());
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(options.outputFile));
    REQUIRE(reader != nullptr);
    CHECK(reader->numChannels == 2);
    CHECK(reader->sampleRate == 44100);
    CHECK(std::abs(reader->lengthInSamples / reader->sampleRate - 1.0) < 0.1);
}

TEST_CASE("Export: AIFF PCM and MP3 rates retain independent stereo content", "[export][mp3]")
{
    ExportFixture f;
    ExportPipeline pipeline;
    for (auto format : {ExportFormat::AIFF, ExportFormat::MP3})
        for (double rate : {44100.0, 48000.0})
            for (int bitrate : {128, 192, 256, 320})
            {
                if (format == ExportFormat::AIFF && bitrate != 320) continue;
                ExportOptions options;
                options.format = format; options.sampleRate = rate; options.mp3BitrateKbps = bitrate;
                auto name = juce::String(static_cast<int>(rate)) + "-" + juce::String(bitrate)
                    + (format == ExportFormat::MP3 ? ".mp3" : ".aif");
                options.outputFile = f.dir.getChildFile(name);
                INFO(name);
                auto error = pipeline.exportMixdown(*f.manager.getEdit(), options);
                INFO(error); REQUIRE(error.isEmpty());
                juce::AudioFormatManager formats; formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(options.outputFile));
                REQUIRE(reader != nullptr);
                CHECK(reader->sampleRate == rate); REQUIRE(reader->numChannels == 2);
                CHECK(std::abs(reader->lengthInSamples / rate - 1.0) < 0.1);
                juce::AudioBuffer<float> buffer(2, static_cast<int>(reader->lengthInSamples));
                REQUIRE(reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true));
                // Frequency projection is insensitive to codec delay but catches
                // silence, corrupted float scaling, and accidentally duplicated channels.
                for (int channel = 0; channel < 2; ++channel)
                {
                    auto magnitude = [&](double frequency)
                    {
                        double real = 0, imag = 0;
                        int start = static_cast<int>(rate * 0.2), count = static_cast<int>(rate * 0.6);
                        for (int i = start; i < start + count; ++i)
                        {
                            auto phase = 2 * juce::MathConstants<double>::pi * frequency * i / rate;
                            real += buffer.getSample(channel,i) * std::cos(phase);
                            imag += buffer.getSample(channel,i) * std::sin(phase);
                        }
                        return 2 * std::hypot(real, imag) / count;
                    };
                    CHECK(magnitude(channel == 0 ? 440 : 660) > 0.17);
                    CHECK(magnitude(channel == 0 ? 440 : 660) < 0.22);
                    CHECK(magnitude(channel == 0 ? 660 : 440) < 0.005);
                }
                if (auto* path = std::getenv("AIZANA_EXPORT_TEST_ARTIFACTS"))
                {
                    juce::File dir(path); REQUIRE(dir.createDirectory().wasOk());
                    REQUIRE(options.outputFile.copyFileTo(dir.getChildFile(name)));
                }
            }
}

TEST_CASE("Export: rejection and cancellation preserve an existing destination", "[export][mp3]")
{
    ExportFixture f;
    ExportPipeline pipeline;
    ExportOptions options;
    options.format = ExportFormat::MP3;
    options.outputFile = f.dir.getChildFile("existing.mp3");
    REQUIRE(options.outputFile.replaceWithText("keep-existing"));
    SECTION("unsupported sample rate") { options.sampleRate = 96000; }
    SECTION("unsupported bitrate") { options.mp3BitrateKbps = 999; }
    SECTION("cancel before render") { options.shouldCancel = [] { return true; }; }
    SECTION("cancel during render")
    {
        int polls = 0;
        options.shouldCancel = [&polls] { return ++polls >= 2; };
        CHECK(pipeline.exportMixdown(*f.manager.getEdit(), options).isNotEmpty());
        CHECK(polls >= 2);
        CHECK(options.outputFile.loadFileAsString() == "keep-existing");
        CHECK(f.dir.findChildFiles(juce::File::findFiles, false).size() == 2);
        return;
    }
    CHECK(pipeline.exportMixdown(*f.manager.getEdit(), options).isNotEmpty());
    CHECK(options.outputFile.loadFileAsString() == "keep-existing");
}

TEST_CASE("Export: invalid output and failed stems report errors", "[export][mp3]")
{
    ExportFixture f;
    ExportPipeline pipeline;
    ExportOptions options;
    options.format = ExportFormat::MP3;
    options.outputFile = f.dir;
    CHECK(pipeline.exportMixdown(*f.manager.getEdit(), options).isNotEmpty());
    options.outputFile = f.dir.getChildFile("input.wav");
    CHECK(pipeline.exportStems(*f.manager.getEdit(), options).isNotEmpty());
}

TEST_CASE("ExportMP3: stems are decodable and duplicate names do not collide", "[export][mp3]")
{
    ExportFixture f;
    auto& edit = *f.manager.getEdit();
    edit.insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(edit), nullptr);
    auto tracks = te::getAudioTracks(edit);
    REQUIRE(tracks.size() >= 2);
    tracks[1]->setSoloIsolate(false);
    ClipEditor editor(f.manager);
    REQUIRE(editor.importAudioFile(f.dir.getChildFile("input.wav"), *tracks[1], 0) != nullptr);
    tracks[0]->setName("same"); tracks[1]->setName("same");
    ExportOptions options; options.format = ExportFormat::MP3;
    options.outputFile = f.dir.getChildFile("stems");
    ExportPipeline pipeline;
    auto error = pipeline.exportStems(*f.manager.getEdit(), options);
    INFO(error); REQUIRE(error.isEmpty());
    CHECK(options.outputFile.getChildFile("same.mp3").existsAsFile());
    CHECK(options.outputFile.getChildFile("same_2.mp3").existsAsFile());
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    for (auto file : options.outputFile.findChildFiles(juce::File::findFiles, false, "*.mp3"))
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        REQUIRE(reader != nullptr); CHECK(reader->numChannels == 2);
    }
}

#include "engine/Mp3Encoder.h"
TEST_CASE("MP3 encoder: input errors, write errors, and mid-encode cancellation", "[export][mp3]")
{
    ExportFixture f;
    auto target = f.dir.getChildFile("direct.mp3");
    CHECK(Mp3Encoder::encode(f.dir.getChildFile("missing.wav"), target, 320, {}).isNotEmpty());
    CHECK(Mp3Encoder::encode(f.dir.getChildFile("input.wav"), f.dir, 320, {}).isNotEmpty());
    int blocks = 0;
    CHECK(Mp3Encoder::encode(f.dir.getChildFile("input.wav"), target, 320,
        [&blocks] { return ++blocks == 3; }).contains("abgebrochen"));
    CHECK(blocks == 3);
}

#include "plugins/BuiltInPluginFormat.h"
#include "fx/DelayProcessor.h"
#include "engine/MixerEngine.h"

namespace {
juce::File qa3Dir(const juce::File& fallback) {
    const auto requested=juce::SystemStats::getEnvironmentVariable("AIZANA_QA_OUTPUT_DIR", {});
    auto f=requested.isEmpty() ? fallback.getChildFile("qa3") : juce::File(requested);
    f.createDirectory(); return f;
}
double qa3Rms(const juce::File& file, double start=0.1, double duration=0.5) {
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(file)); REQUIRE(r);
    juce::AudioBuffer<float> b(2,(int)(duration*r->sampleRate));
    REQUIRE(r->read(&b,0,b.getNumSamples(),(juce::int64)(start*r->sampleRate),true,true));
    return b.getRMSLevel(0,0,b.getNumSamples());
}
}
TEST_CASE("QA3 export retains audible delay after the final clip", "[qa3]") {
    ExportFixture f; auto& e=*f.manager.getEdit(); auto* t=te::getAudioTracks(e)[0];
    auto ptr=e.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName,BuiltInPluginFormat::getDescriptionFor("Aizana Delay"));
    REQUIRE(ptr); t->pluginList.insertPlugin(ptr,-1,nullptr);
    auto* ext=dynamic_cast<te::ExternalPlugin*>(ptr.get()); REQUIRE(ext); ext->initialiseFully();
    auto* delay=dynamic_cast<DelayProcessor*>(ext->getAudioPluginInstance()); REQUIRE(delay);
    delay->setTimeMs(350); delay->setFeedback(50); delay->setMix(100); ext->flushPluginStateToValueTree();
    auto out=qa3Dir(f.dir);
    REQUIRE(f.manager.saveEdit(out.getChildFile("delay-session/delay-session.tracktion")).wasOk());
    ExportPipeline pipeline; ExportOptions o; o.outputFile=out.getChildFile("delay-normal.wav");
    REQUIRE(pipeline.exportMixdown(e,o).isEmpty());
    {
        EditManager reopened;
        REQUIRE(reopened.loadEdit(out.getChildFile("delay-session/delay-session.tracktion")));
        ExportOptions again; again.outputFile=out.getChildFile("delay-reopened.wav");
        REQUIRE(pipeline.exportMixdown(*reopened.getEdit(),again).isEmpty());
        CHECK(std::abs(qa3Rms(o.outputFile,0.5,0.3)-qa3Rms(again.outputFile,0.5,0.3))<0.0001);
    }
    // Silent MIDI clip only extends the export interval, allowing the same FX to decay.
    te::ClipPosition pos{tracktion::core::TimeRange(tracktion::core::TimePosition::fromSeconds(1),tracktion::core::TimePosition::fromSeconds(3)),{}};
    REQUIRE(t->insertNewClip(te::TrackItem::Type::midi,"tail window",pos,nullptr));
    o.outputFile=out.getChildFile("delay-padded-control.wav"); REQUIRE(pipeline.exportMixdown(e,o).isEmpty());
    const auto tail=qa3Rms(o.outputFile,1.1,0.2); REQUIRE(tail>0.01);
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(out.getChildFile("delay-normal.wav"))); REQUIRE(r);
    INFO("normal duration=" << r->lengthInSamples/r->sampleRate << " control tail RMS=" << tail);
    CHECK(r->lengthInSamples/r->sampleRate >= 5.0);
    CHECK(qa3Rms(out.getChildFile("delay-normal.wav"),1.1,0.2)>0.01);
}
TEST_CASE("QA3 routed track stem contains its audio", "[qa3]") {
    ExportFixture f; auto& e=*f.manager.getEdit(); auto* t=te::getAudioTracks(e)[0]; t->setName("Source");
    MixerEngine mixer(f.manager); auto* bus=mixer.createBusChannel("Bus"); REQUIRE(bus); mixer.routeTrackToBus(*t,*bus);
    auto second=e.insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(e),nullptr);
    REQUIRE(second); second->setName("Second"); second->setSoloIsolate(false);
    ClipEditor editor(f.manager);
    REQUIRE(editor.importAudioFile(f.dir.getChildFile("input.wav"),*second,0));
    mixer.routeTrackToBus(*second,*bus);
    ExportPipeline pipeline; ExportOptions o; o.outputFile=qa3Dir(f.dir).getChildFile("routed-mix.wav");
    REQUIRE(pipeline.exportMixdown(e,o).isEmpty()); const auto mix=qa3Rms(o.outputFile); REQUIRE(mix>0.01);
    o.outputFile=qa3Dir(f.dir).getChildFile("routed-stems"); REQUIRE(pipeline.exportStems(e,o).isEmpty());
    const auto stem=qa3Rms(o.outputFile.getChildFile("Source.wav"));
    INFO("mix RMS=" << mix << " source stem RMS=" << stem);
    CHECK(stem>0.01);
    CHECK_THAT(stem, Catch::Matchers::WithinAbs(mix * 0.5, 0.0001));
    CHECK_THAT(qa3Rms(o.outputFile.getChildFile("Second.wav")), Catch::Matchers::WithinAbs(stem, 0.0001));
    CHECK_THAT(qa3Rms(o.outputFile.getChildFile("Bus.wav")), Catch::Matchers::WithinAbs(mix, 0.0001));
    CHECK(t->getOutput().getDestinationTrack()==bus);
    CHECK(second->getOutput().getDestinationTrack()==bus);
}


TEST_CASE("Export: FX stems retain send inputs without dry leakage", "[export][stem-sends]")
{
    ExportFixture f;
    auto& edit = *f.manager.getEdit();
    auto* source = te::getAudioTracks(edit)[0];
    source->setName("Source");
    MixerEngine mixer(f.manager);
    auto* fx = mixer.createFXChannel("FX");
    REQUIRE(fx);
    auto* send = mixer.createFXSend(*source, *fx, -6.0f);
    REQUIRE(send);
    auto* second = mixer.createFXChannel("Second FX");
    REQUIRE(second);
    REQUIRE(mixer.createFXSend(*fx, *second, -6.0f));
    auto* bus = mixer.createBusChannel("Source Bus");
    REQUIRE(bus);
    mixer.routeTrackToBus(*source, *bus);

    const auto ratio = juce::Decibels::decibelsToGain(-6.0);
    double fxRatio = ratio;
    SECTION("post-fader send") { mixer.setVolume(*source, -6.0f); }
    SECTION("pre-fader send")
    {
        mixer.setVolume(*source, -6.0f);
        mixer.setFXSendPreFader(*source, *fx, true);
        fxRatio = 1.0;
    }
    SECTION("disabled send")
    {
        mixer.setFXSendEnabled(*source, *fx, false);
        fxRatio = 0.0;
    }
    const auto sendLevel = mixer.getFXSendLevel(*source, *fx);
    const auto sendEnabled = mixer.isFXSendEnabled(*source, *fx);
    const auto undoDescription = edit.getUndoManager().getUndoDescription();
    ExportPipeline pipeline;
    ExportOptions options;
    options.outputFile = f.dir.getChildFile("stems");
    REQUIRE(pipeline.exportStems(edit, options).isEmpty());
    const auto dry = qa3Rms(options.outputFile.getChildFile("Source.wav"));
    REQUIRE(dry > 0.01);
    CHECK_THAT(qa3Rms(options.outputFile.getChildFile("FX.wav")),
               Catch::Matchers::WithinAbs(dry * fxRatio, 0.0001));
    CHECK_THAT(qa3Rms(options.outputFile.getChildFile("Second FX.wav")),
               Catch::Matchers::WithinAbs(dry * fxRatio * ratio, 0.0001));
    CHECK_THAT(qa3Rms(options.outputFile.getChildFile("Source Bus.wav")),
               Catch::Matchers::WithinAbs(dry, 0.0001));
    CHECK(source->getOutput().getDestinationTrack() == bus);
    CHECK(fx->getOutput().getDestinationTrack() == nullptr);
    CHECK(te::getAudioTracks(edit).size() == 4);
    CHECK(mixer.getFXSendLevel(*source, *fx) == sendLevel);
    CHECK(mixer.isFXSendEnabled(*source, *fx) == sendEnabled);
    CHECK(edit.getUndoManager().getUndoDescription() == undoDescription);
}


TEST_CASE("Export: wet delay stem excludes direct source and retains decay", "[export][stem-sends]")
{
    ExportFixture f;
    auto& edit = *f.manager.getEdit();
    auto* source = te::getAudioTracks(edit)[0];
    MixerEngine mixer(f.manager);
    auto* fx = mixer.createFXChannel("Wet Delay");
    REQUIRE(fx);
    REQUIRE(mixer.createFXSend(*source, *fx, 0.0f));
    auto plugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName,
        BuiltInPluginFormat::getDescriptionFor("Aizana Delay"));
    REQUIRE(plugin);
    fx->pluginList.insertPlugin(plugin, -1, nullptr);
    auto* external = dynamic_cast<te::ExternalPlugin*>(plugin.get());
    REQUIRE(external);
    external->initialiseFully();
    auto* delay = dynamic_cast<DelayProcessor*>(external->getAudioPluginInstance());
    REQUIRE(delay);
    delay->setTimeMs(350);
    delay->setFeedback(50);
    delay->setMix(100);
    external->flushPluginStateToValueTree();
    ExportPipeline pipeline;
    ExportOptions options;
    options.outputFile = f.dir.getChildFile("wet-stems");
    REQUIRE(pipeline.exportStems(edit, options).isEmpty());
    auto file = options.outputFile.getChildFile("Wet Delay.wav");
    CHECK(qa3Rms(file, 0.05, 0.1) < 0.00001);
    CHECK(qa3Rms(file, 0.4, 0.1) > 0.01);
    CHECK(qa3Rms(file, 1.1, 0.2) > 0.01);
}

#include "fx/CompressorProcessor.h"
TEST_CASE("Export: stem keeps external sidechain detector signal", "[export][stem-sidechain]")
{
    ExportFixture f;
    auto& edit = *f.manager.getEdit();
    auto* key = te::getAudioTracks(edit)[0];
    key->setName("Key");
    auto target = edit.insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(edit), nullptr);
    REQUIRE(target);
    target->setName("Compressed");
    target->setSoloIsolate(false);
    ClipEditor editor(f.manager);
    auto* clip = editor.importAudioFile(f.dir.getChildFile("input.wav"), *target, 0);
    REQUIRE(clip);
    clip->setGainDB(-30.0f); // Too quiet to trigger the chosen threshold itself.
    auto plugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName,
        BuiltInPluginFormat::getDescriptionFor("Aizana Compressor"));
    REQUIRE(plugin);
    target->pluginList.insertPlugin(plugin, -1, nullptr);
    auto* external = dynamic_cast<te::ExternalPlugin*>(plugin.get());
    REQUIRE(external);
    external->initialiseFully();
    auto* compressor = dynamic_cast<CompressorProcessor*>(external->getAudioPluginInstance());
    REQUIRE(compressor);
    // Write host parameters so settings survive the export snapshot.
    auto setParameter = [&] (const char* id, float value)
    {
        for (auto* parameter : compressor->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                if (ranged->paramID == id)
                {
                    ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
                    return;
                }
        FAIL("Missing compressor parameter: " << id);
    };
    setParameter("threshold", -30);
    setParameter("ratio", 10);
    setParameter("attack", 1);
    setParameter("knee", 0);
    compressor->setSidechainKeyEnabled(true);
    MixerEngine mixer(f.manager);
    mixer.setSidechainSource(*plugin, *key);
    external->flushPluginStateToValueTree();
    ExportPipeline pipeline;
    ExportOptions options;
    options.outputFile = f.dir.getChildFile("keyed");
    REQUIRE(pipeline.exportStems(edit, options).isEmpty());
    const auto keyed = qa3Rms(options.outputFile.getChildFile("Compressed.wav"));
    mixer.clearSidechainSource(*plugin);
    options.outputFile = f.dir.getChildFile("unkeyed");
    REQUIRE(pipeline.exportStems(edit, options).isEmpty());
    const auto unkeyed = qa3Rms(options.outputFile.getChildFile("Compressed.wav"));
    REQUIRE(unkeyed > 0.001);
    CHECK(keyed > 0.00001);
    CHECK(keyed < unkeyed * 0.5);
}

TEST_CASE("Export: submix child and its FX remain separate stems", "[export][stem-sends]")
{
    ExportFixture f;
    auto& edit = *f.manager.getEdit();
    auto* source = te::getAudioTracks(edit)[0];
    source->setName("Child");
    auto folder = edit.insertNewFolderTrack(te::TrackInsertPoint::getEndOfTracks(edit), nullptr, true);
    REQUIRE(folder);
    edit.moveTrack(source, te::TrackInsertPoint{folder.get(), nullptr});
    REQUIRE(source->isPartOfSubmix());
    MixerEngine mixer(f.manager);
    auto* fx = mixer.createFXChannel("FX");
    REQUIRE(fx);
    REQUIRE(mixer.createFXSend(*source, *fx, -6.0f));
    ExportPipeline pipeline;
    ExportOptions options;
    options.outputFile = f.dir.getChildFile("submix-stems");
    REQUIRE(pipeline.exportStems(edit, options).isEmpty());
    const auto dry = qa3Rms(options.outputFile.getChildFile("Child.wav"));
    REQUIRE(dry > 0.01);
    CHECK_THAT(qa3Rms(options.outputFile.getChildFile("FX.wav")),
        Catch::Matchers::WithinAbs(dry * juce::Decibels::decibelsToGain(-6.0), 0.0001));
    CHECK(source->getParentFolderTrack() == folder.get());
}
