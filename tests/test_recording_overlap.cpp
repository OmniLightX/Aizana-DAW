#include <catch2/catch_test_macros.hpp>
#include "engine/EditManager.h"
#include "engine/ClipEditor.h"
#include "engine/ExportPipeline.h"
#include "engine/AizanaEngineBehaviour.h"

TEST_CASE ("New non-loop recordings replace playback only within their overlap", "[recording-overlap]")
{
    auto dir = juce::File ("/tmp").getNonexistentChildFile ("aizana-overlap", {}, false);
    REQUIRE (dir.createDirectory().wasOk());
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();
    auto* track = te::getAudioTracks (edit)[0];
    ClipEditor editor (em);
    auto write = [&] (const char* name, float value, int seconds)
    {
        auto file = dir.getChildFile (name);
        auto stream = file.createOutputStream();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), 44100, 1, 24, {}, 0));
        REQUIRE (writer); stream.release();
        juce::AudioBuffer<float> data (1, seconds * 44100);
        for (int i = 0; i < data.getNumSamples(); ++i) data.setSample (0, i, value);
        REQUIRE (writer->writeFromAudioSampleBuffer (data, 0, data.getNumSamples()));
        return file;
    };
    auto* first = editor.importAudioFile (write ("first.wav", 0.1f, 3), *track, 0);
    auto* second = editor.importAudioFile (write ("second.wav", 0.3f, 1), *track, 1);
    REQUIRE (first); REQUIRE (second);
    edit.engine.getEngineBehaviour().newClipAdded (*second, true);
    auto checkRender = [&] (float middleRatio)
    {
        ExportOptions options;
        options.outputFile = dir.getChildFile ("mix.wav");
        options.sampleRate = 44100; options.bitDepth = 24;
        ExportPipeline pipeline;
        REQUIRE (pipeline.exportMixdown (*em.getEdit(), options).isEmpty());
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (options.outputFile));
        REQUIRE (reader);
        juce::AudioBuffer<float> rendered (2, (int) reader->lengthInSamples);
        REQUIRE (reader->read (&rendered, 0, rendered.getNumSamples(), 0, true, true));
        auto level = [&] (double time) { return rendered.getRMSLevel (0, (int) (time * 44100), 4410); };
        CHECK (std::abs (level (1.2) / level (0.2) - middleRatio) < 0.02f);
        CHECK (std::abs (level (2.2) / level (0.2) - 1.0f) < 0.02f);
    };
    checkRender (3.0f);
    edit.getUndoManager().beginNewTransaction ("Choose earlier recording");
    AizanaEngineBehaviour::preferRecording (*first);
    edit.getUndoManager().beginNewTransaction();
    checkRender (1.0f);
    REQUIRE (edit.getUndoManager().undo());
    checkRender (3.0f);
    REQUIRE (edit.getUndoManager().redo());
    checkRender (1.0f);
    CHECK (track->getClips().size() == 2);
    CHECK (first->getSourceFileReference().getFile().existsAsFile());
    const auto session = dir.getChildFile ("session.tracktion");
    REQUIRE (em.saveEdit (session).wasOk());
    REQUIRE (em.loadEdit (session));
    checkRender (1.0f);
    dir.deleteRecursively();
}

TEST_CASE ("Recording priority respects tracks, clip selection, mute and looping", "[recording-overlap]")
{
    EditManager em; em.createNewEdit();
    auto& edit = *em.getEdit();
    auto* track = te::getAudioTracks (edit)[0];
    auto other = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);
    auto add = [&] (te::AudioTrack& t, double start, double end)
    {
        return t.insertNewClip (te::TrackItem::Type::wave, "take",
            { { tracktion::TimePosition::fromSeconds (start), tracktion::TimePosition::fromSeconds (end) }, {} }, nullptr);
    };
    auto* base = add (*track, 0, 5);
    auto* a = add (*track, 1, 3);
    auto* b = add (*track, 2, 4);
    auto* unrelated = add (*other, 0, 5);
    REQUIRE (base); REQUIRE (a); REQUIRE (b); REQUIRE (unrelated);
    auto& behaviour = edit.engine.getEngineBehaviour();
    CHECK (behaviour.getClipPlaybackMuteTimes (*base, nullptr).isEmpty());
    edit.getTransport().looping = true;
    behaviour.newClipAdded (*a, true);
    // A new loop-take container also wins over a previous recording; its
    // internal take/comp selection is unaffected.
    CHECK (AizanaEngineBehaviour::recordingPriority (*a) > 0);
    edit.getTransport().looping = false;
    behaviour.newClipAdded (*a, true);
    behaviour.newClipAdded (*b, true);
    auto ranges = behaviour.getClipPlaybackMuteTimes (*base, nullptr);
    REQUIRE (ranges.size() == 1);
    CHECK (ranges[0].getStart().inSeconds() == 1.0);
    CHECK (ranges[0].getEnd().inSeconds() == 4.0);
    CHECK (behaviour.getClipPlaybackMuteTimes (*unrelated, nullptr).isEmpty());
    juce::Array<te::Clip*> onlyBase { base };
    CHECK (behaviour.getClipPlaybackMuteTimes (*base, &onlyBase).isEmpty());
    b->setMuted (true);
    ranges = behaviour.getClipPlaybackMuteTimes (*base, nullptr);
    REQUIRE (ranges.size() == 1);
    // Muting the selected take must not unexpectedly reveal an older performance.
    CHECK (ranges[0].getEnd().inSeconds() == 4.0);
}
