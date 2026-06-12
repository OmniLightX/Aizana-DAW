// test_stem_import.cpp — Phase 3, Iteration 1, Slice 3 (AI-02/03/04):
// SessionPlan → real session via AICommandBridge, everything in ONE undo step.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ai/StemImportEngine.h"
#include "engine/EditManager.h"

namespace
{
    constexpr double kSr = 44100.0;

    juce::File makeTempDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("cloudes_import_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
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
