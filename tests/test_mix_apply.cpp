// test_mix_apply.cpp — Phase 4 / Iteration 1, Slice 3:
// MixPlan → real Tracktion Engine mutations via the AICommandBridge, all in ONE
// undo step. Full path: import a session → read profiles → heuristic mix → apply.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ai/MixApplyEngine.h"
#include "ai/StemImportEngine.h"
#include "engine/EditManager.h"

using cloudes::MixingEngine;
using cloudes::MixPlan;
using cloudes::TrackMixProfile;

namespace
{
    constexpr double kSr = 44100.0;

    juce::File makeTempDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("cloudes_mixapply_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
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

    // Count Cloudes built-in FX in a plugin list by the wrapped processor's name.
    int countBuiltIn (te::PluginList& list, const juce::String& procName)
    {
        int n = 0;
        for (auto* p : list)
            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p))
            {
                ext->initialiseFully();
                if (auto* inst = ext->getAudioPluginInstance())
                    if (inst->getName() == procName)
                        ++n;
            }
        return n;
    }

    // Build a 4-stem session (vocals/bass/drums/synth) and return the edit.
    te::Edit& buildSession (EditManager& em, MixerEngine& mixer, AICommandBridge& bridge,
                            StemImportEngine& importer, juce::File& dirOut)
    {
        dirOut = makeTempDir();
        juce::Array<juce::File> files;
        files.add (writeToneWav (dirOut, "Lead Vocal.wav", 440.0, 0.30f));
        files.add (writeToneWav (dirOut, "Bass.wav",        60.0, 0.50f));
        files.add (writeToneWav (dirOut, "Drums.wav",      120.0, 0.45f));
        files.add (writeToneWav (dirOut, "Synth.wav",     1000.0, 0.40f));

        const auto stems = StemAnalyzer::analyzeFiles (files);
        const auto plan  = SessionPlanner::plan (stems);
        importer.applyPlan (plan);
        bridge.applyPendingCommands();
        return *em.getEdit();
    }
}

TEST_CASE ("MixApplyEngine: reader profiles the stem tracks and flags the lead vocal", "[mixapply]")
{
    EditManager em; em.createNewEdit();
    MixerEngine mixer (em);
    AICommandBridge bridge (em);
    StemImportEngine importer (em, mixer, bridge);
    MixApplyEngine applyEngine (em, bridge);

    juce::File dir;
    buildSession (em, mixer, bridge, importer, dir);

    const auto profiles = applyEngine.readProfiles();

    // Four stem tracks profiled (buses are skipped).
    REQUIRE (profiles.size() == 4);

    int leads = 0;
    bool vocalIsLead = false;
    for (const auto& p : profiles)
    {
        if (p.isLeadVocal) ++leads;
        if (p.name == "Lead Vocal") vocalIsLead = p.isLeadVocal;
        REQUIRE (p.rmsDb > -120.0);          // levels were measured
    }
    CHECK (leads == 1);
    CHECK (vocalIsLead);

    dir.deleteRecursively();
}

TEST_CASE ("MixApplyEngine: full mix applies as built-in FX, one undo reverts", "[mixapply]")
{
    EditManager em; em.createNewEdit();
    MixerEngine mixer (em);
    AICommandBridge bridge (em);
    StemImportEngine importer (em, mixer, bridge);
    MixApplyEngine applyEngine (em, bridge);

    juce::File dir;
    auto& edit = buildSession (em, mixer, bridge, importer, dir);

    auto* vox = findTrackByName (edit, "Lead Vocal");
    auto* bass = findTrackByName (edit, "Bass");
    REQUIRE (vox != nullptr);
    REQUIRE (bass != nullptr);

    // Baselines before mixing.
    const int masterCompBefore = countBuiltIn (edit.getMasterPluginList(), "CompressorProcessor");
    REQUIRE (countBuiltIn (vox->pluginList, "EQProcessor")       == 0);
    REQUIRE (countBuiltIn (vox->pluginList, "DeEsserProcessor")  == 0);

    // Heuristic mix → apply through the bridge (one batch).
    const auto profiles = applyEngine.readProfiles();
    const auto plan     = MixingEngine::mix (profiles);
    REQUIRE (! plan.isEmpty());

    applyEngine.applyPlan (plan);
    bridge.applyPendingCommands();

    // Lead vocal got the full chain: one EQ (all bands folded in), a de-esser,
    // and two serial compressors.
    CHECK (countBuiltIn (vox->pluginList, "EQProcessor")      == 1);
    CHECK (countBuiltIn (vox->pluginList, "DeEsserProcessor") == 1);
    CHECK (countBuiltIn (vox->pluginList, "CompressorProcessor") == 2);

    // Bass got its steadiness compressor; master got the glue.
    CHECK (countBuiltIn (bass->pluginList, "CompressorProcessor") == 1);
    CHECK (countBuiltIn (edit.getMasterPluginList(), "CompressorProcessor") == masterCompBefore + 1);

    // AI-04: ONE undo removes the whole mix.
    em.getUndoManager().undo();
    CHECK (countBuiltIn (vox->pluginList, "EQProcessor")      == 0);
    CHECK (countBuiltIn (vox->pluginList, "DeEsserProcessor") == 0);
    CHECK (countBuiltIn (bass->pluginList, "CompressorProcessor") == 0);
    CHECK (countBuiltIn (edit.getMasterPluginList(), "CompressorProcessor") == masterCompBefore);

    // …and redo restores it.
    em.getUndoManager().redo();
    CHECK (countBuiltIn (vox->pluginList, "DeEsserProcessor") == 1);
    CHECK (countBuiltIn (edit.getMasterPluginList(), "CompressorProcessor") == masterCompBefore + 1);

    dir.deleteRecursively();
}

TEST_CASE ("MixApplyEngine: empty plan is a no-op", "[mixapply]")
{
    EditManager em; em.createNewEdit();
    AICommandBridge bridge (em);
    MixApplyEngine applyEngine (em, bridge);

    applyEngine.applyPlan (MixPlan{});
    bridge.applyPendingCommands();

    CHECK (bridge.getPendingBatchCount() == 0);
}
