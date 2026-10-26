// test_stem_import.cpp — Phase 3, Iteration 1, Slice 3 (AI-02/03/04):
// SessionPlan → real session via AICommandBridge, everything in ONE undo step.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ai/StemImportEngine.h"
#include "engine/EditManager.h"
#include "engine/ClipEditor.h"

namespace
{
    constexpr double kSr = 44100.0;

    juce::File makeTempDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_import_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        REQUIRE (dir.createDirectory());
        return dir;
    }

    juce::File writeToneWav (const juce::File& dir, const juce::String& name,
                             double freq, float amp, double seconds = 2.0)
    {
        auto file = dir.getChildFile (name);
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), kSr, 1, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();

        juce::AudioBuffer<float> buf (1, (int) (seconds * kSr));
        for (int i = 0; i < buf.getNumSamples(); ++i)
            buf.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                         * freq * i / kSr));
        REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()));
        return file;
    }

    te::AudioTrack* findTrackByName (te::Edit& edit, const juce::String& name)
    {
        for (auto* t : te::getAudioTracks (edit))
            if (t->getName() == name)
                return t;
        return nullptr;
    }
}

TEST_CASE ("StemImportEngine: full pipeline files→plan→session, one undo step", "[stemimport]")
{
    EditManager em;
    em.createNewEdit();
    MixerEngine mixer (em);
    AICommandBridge bridge (em);
    StemImportEngine importer (em, mixer, bridge);

    auto* edit = em.getEdit();
    const int initialTracks = te::getAudioTracks (*edit).size();

    auto dir = makeTempDir();
    juce::Array<juce::File> files;
    files.add (writeToneWav (dir, "Kick In.wav",    60.0,  0.6f));
    files.add (writeToneWav (dir, "808 Sub.wav",    50.0,  0.5f));
    files.add (writeToneWav (dir, "Vocal Mic.wav", 440.0,  0.3f));
    files.add (writeToneWav (dir, "Track 9.wav",  1000.0,  0.4f));   // → Tier 2 "Other"

    // Pipeline: analyse → plan → apply (bridge batch)
    const auto stems = StemAnalyzer::analyzeFiles (files);
    const auto plan  = SessionPlanner::plan (stems);

    REQUIRE (plan.tracks.size() == 4);
    REQUIRE (plan.groups.contains ("Drums"));
    REQUIRE (plan.groups.contains ("Bass"));
    REQUIRE (plan.groups.contains ("Vocals"));

    importer.applyPlan (plan);
    bridge.applyPendingCommands();   // message thread in tests — apply synchronously

    // --- Tracks created, named after their FILES, clip with absolute source ref
    auto* kick = findTrackByName (*edit, "Kick In");
    REQUIRE (kick != nullptr);
    REQUIRE (kick->getClips().size() == 1);
    CHECK (kick->getClips()[0]->getSourceFileReference().getFile() == files[0]);

    auto* vox = findTrackByName (*edit, "Vocal Mic");
    REQUIRE (vox != nullptr);

    // --- Gain staging applied (fader round-trip tolerance)
    const auto plannedKickGain = plan.tracks[0].gainDb;
    REQUIRE_THAT ((double) mixer.getVolume (*kick),
                  Catch::Matchers::WithinAbs (plannedKickGain, 0.25));

    // --- Buses exist, tagged as bus, at the END of the track list
    auto* drumsBus = findTrackByName (*edit, "Drums");
    REQUIRE (drumsBus != nullptr);
    CHECK (MixerEngine::isBusChannel (*drumsBus));

    const auto all = te::getAudioTracks (*edit);
    const int busIndex   = all.indexOf (drumsBus);
    const int trackIndex = all.indexOf (kick);
    CHECK (busIndex > trackIndex);   // buses appended after stem tracks

    // --- Routing: kick → Drums bus (TrackOutput points at the bus track)
    CHECK (kick->getOutput().getDestinationTrack() == drumsBus);
    // "Other" stem routes direct to master (no destination track)
    auto* other = findTrackByName (*edit, "Track 9");
    REQUIRE (other != nullptr);
    CHECK (other->getOutput().getDestinationTrack() == nullptr);

    // --- AI-04: ONE undo reverts the entire setup
    em.getUndoManager().undo();
    CHECK (te::getAudioTracks (*edit).size() == initialTracks);
    CHECK (findTrackByName (*edit, "Kick In") == nullptr);
    CHECK (findTrackByName (*edit, "Drums") == nullptr);

    // …and redo restores it
    em.getUndoManager().redo();
    CHECK (findTrackByName (*edit, "Kick In") != nullptr);
    CHECK (findTrackByName (*edit, "Drums") != nullptr);

    dir.deleteRecursively();
}

TEST_CASE ("StemImportEngine: empty plan is a no-op", "[stemimport]")
{
    EditManager em;
    em.createNewEdit();
    MixerEngine mixer (em);
    AICommandBridge bridge (em);
    StemImportEngine importer (em, mixer, bridge);

    const int initialTracks = te::getAudioTracks (*em.getEdit()).size();

    importer.applyPlan (SessionPlan{});
    bridge.applyPendingCommands();

    CHECK (te::getAudioTracks (*em.getEdit()).size() == initialTracks);
    CHECK (bridge.getPendingBatchCount() == 0);
}

TEST_CASE ("Audio import preserves full duration and requested track and position", "[stemimport][import-regression]")
{
    EditManager em;
    em.createNewEdit();
    ClipEditor editor (em);
    auto* edit = em.getEdit();
    auto* first = te::getAudioTracks (*edit)[0];
    auto target = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    REQUIRE (target != nullptr);
    auto dir = makeTempDir();
    auto file = writeToneWav (dir, "long.wav", 220.0, 0.2f, 45.0);
    auto* clip = editor.importAudioFile (file, *target, 7.5);
    REQUIRE (clip != nullptr);
    CHECK (first->getClips().isEmpty());
    REQUIRE (target->getClips().size() == 1);
    CHECK_THAT (clip->getPosition().getStart().inSeconds(), Catch::Matchers::WithinAbs (7.5, 0.00001));
    CHECK_THAT (clip->getPosition().getEnd().inSeconds(), Catch::Matchers::WithinAbs (52.5, 0.00001));
    CHECK (clip->getSourceFileReference().getFile() == file);
    em.getUndoManager().undo();
    CHECK (target->getClips().isEmpty());
    dir.deleteRecursively();
}

TEST_CASE ("Audio import rejects corrupt WAV without adding a clip", "[stemimport][import-regression]")
{
    EditManager em;
    em.createNewEdit();
    ClipEditor editor (em);
    auto* track = te::getAudioTracks (*em.getEdit())[0];
    auto dir = makeTempDir();
    auto file = dir.getChildFile ("corrupt.wav");
    REQUIRE (file.replaceWithText ("RIFF invalid WAV data"));
    const auto count = track->getClips().size();
    CHECK (editor.importAudioFile (file, *track, 0.0) == nullptr);
    CHECK (track->getClips().size() == count);
    dir.deleteRecursively();
}

TEST_CASE ("Quiet stem preview gain equals applied fader gain", "[stemimport][import-regression]")
{
    EditManager em;
    em.createNewEdit();
    MixerEngine mixer (em);
    AICommandBridge bridge (em);
    StemImportEngine importer (em, mixer, bridge);
    auto dir = makeTempDir();
    auto file = writeToneWav (dir, "Keys.wav", 440.0, 0.02f);
    auto plan = SessionPlanner::plan (StemAnalyzer::analyzeFiles ({ file }));
    REQUIRE (plan.tracks.size() == 1);
    REQUIRE_THAT (plan.tracks[0].gainDb, Catch::Matchers::WithinAbs (6.0, 0.001));
    importer.applyPlan (plan);
    bridge.applyPendingCommands();
    auto* track = findTrackByName (*em.getEdit(), "Keys");
    REQUIRE (track != nullptr);
    CHECK_THAT ((double) mixer.getVolume (*track), Catch::Matchers::WithinAbs (plan.tracks[0].gainDb, 0.001));
    dir.deleteRecursively();
}

// Regression tests from the second QA pass.
TEST_CASE ("QA2 corrupt stem must not create empty tracks", "[qa2]")
{
    EditManager em; em.createNewEdit();
    MixerEngine mixer (em); AICommandBridge bridge (em);
    StemImportEngine importer (em, mixer, bridge);
    auto dir = makeTempDir();
    auto file = dir.getChildFile ("Kick.wav");
    REQUIRE (file.replaceWithText ("RIFF invalid WAV"));
    int before = te::getAudioTracks (*em.getEdit()).size();
    auto plan = SessionPlanner::plan (StemAnalyzer::analyzeFiles ({file}));
    CHECK (plan.errors.size() == 1);
    importer.applyPlan (plan); bridge.applyPendingCommands();
    INFO ("before=" << before << " after=" << te::getAudioTracks (*em.getEdit()).size());
    CHECK (te::getAudioTracks (*em.getEdit()).size() == before);
    dir.deleteRecursively();
}

TEST_CASE ("QA2 stereo anti-phase audio must retain measured channel energy", "[qa2]")
{
    auto dir = makeTempDir();
    auto file = dir.getChildFile ("Keys.wav");
    juce::WavAudioFormat wav;
    auto stream = file.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), kSr, 2, 24, {}, 0));
    REQUIRE (writer != nullptr); stream.release();
    juce::AudioBuffer<float> buf (2, (int) kSr);
    for (int i=0; i<buf.getNumSamples(); ++i)
    {
        float x = 0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / kSr);
        buf.setSample (0,i,x); buf.setSample (1,i,-x);
    }
    REQUIRE (writer->writeFromAudioSampleBuffer (buf,0,buf.getNumSamples())); writer.reset();
    auto info = StemAnalyzer::analyzeFile (file);
    INFO ("actual RMS=" << info.rmsDb << " peak=" << info.peakDb);
    CHECK_THAT (info.rmsDb, Catch::Matchers::WithinAbs (-9.0309, 0.01));
    CHECK_THAT (info.peakDb, Catch::Matchers::WithinAbs (-6.0206, 0.01));
    dir.deleteRecursively();
}

#include "ai/MixApplyEngine.h"
TEST_CASE ("QA2 KI Mix measures retained audio after trimming", "[qa2]")
{
    EditManager em; em.createNewEdit();
    MixerEngine mixer (em); AICommandBridge bridge (em);
    MixApplyEngine apply (em,mixer,bridge);
    ClipEditor editor (em);
    auto dir = makeTempDir(); auto file = dir.getChildFile ("Keys.wav");
    juce::WavAudioFormat wav; auto stream = file.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), kSr, 1, 24, {}, 0));
    REQUIRE (writer != nullptr); stream.release();
    juce::AudioBuffer<float> buf (1, 2*(int)kSr);
    for(int i=0;i<buf.getNumSamples();++i)
        buf.setSample(0,i,(i<kSr ? 0.5f : 0.005f)*std::sin(2.0*juce::MathConstants<double>::pi*440.0*i/kSr));
    REQUIRE(writer->writeFromAudioSampleBuffer(buf,0,buf.getNumSamples())); writer.reset();
    auto* track=te::getAudioTracks(*em.getEdit())[0];
    auto* clip=editor.importAudioFile(file,*track,0.0); REQUIRE(clip != nullptr);
    editor.trimClip(*clip,1.0,2.0);
    REQUIRE_THAT(clip->getPosition().getOffset().inSeconds(),Catch::Matchers::WithinAbs(1.0,0.0001));
    auto profiles=apply.readProfiles(); REQUIRE(profiles.size()==1);
    INFO("retained audio RMS should be -49.03, measured=" << profiles[0].rmsDb);
    CHECK_THAT(profiles[0].rmsDb,Catch::Matchers::WithinAbs(-49.0309,0.02));
    dir.deleteRecursively();
}

TEST_CASE ("QA2 import rejects a file removed after preview without creating tracks", "[qa2]")
{
    EditManager em; em.createNewEdit();
    MixerEngine mixer(em); AICommandBridge bridge(em);
    StemImportEngine importer(em,mixer,bridge);
    auto dir=makeTempDir(); auto file=writeToneWav(dir,"Kick.wav",100.0,0.2f);
    auto plan=SessionPlanner::plan(StemAnalyzer::analyzeFiles({file}));
    REQUIRE(plan.tracks.size()==1);
    REQUIRE(file.deleteFile());
    bool reported=false;
    importer.onError=[&](const juce::String& error){reported=error.contains("Kick.wav");};
    const auto before=te::getAudioTracks(*em.getEdit()).size();
    importer.applyPlan(plan); bridge.applyPendingCommands();
    CHECK(reported);
    CHECK(te::getAudioTracks(*em.getEdit()).size()==before);
    dir.deleteRecursively();
}
