// test_intent_engine.cpp — Phase 2, Block B: IntentEngine
//
// Verifies that intents produce REAL, editable Tracktion-Engine mechanic applied as
// ONE undo transaction via the AICommandBridge:
//   - a level intent changes the target track's volume; one undo reverts it
//   - a character intent inserts Cloudes' OWN EQ (editable — Fix 3); one undo removes it
//   - a loudness intent inserts Cloudes' OWN glue compressor on the master; one undo reverts
//   - category / label / intentsFor mapping is consistent
//
// Fix 3 (2026-06-05): intents now insert Cloudes' built-in FX (EQProcessor /
// CompressorProcessor / ReverbProcessor) wrapped as ExternalPlugin via
// BuiltInPluginFormat — NOT native te::*Plugin — so the inserted mechanic is editable.

#include <catch2/catch_test_macros.hpp>

#include "engine/EditManager.h"
#include "engine/AICommandBridge.h"
#include "ai/IntentEngine.h"

namespace
{
    // Count Cloudes built-in FX in a plugin list, identified by the wrapped processor's
    // name ("EQProcessor", "CompressorProcessor", "ReverbProcessor"). They are inserted
    // as TE ExternalPlugins (getPluginType() == ExternalPlugin::xmlTypeName), so we look
    // through the wrapper at the live AudioPluginInstance.
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

    void applyPlan (AICommandBridge& bridge, const IntentEngine::Plan& plan)
    {
        REQUIRE (plan.valid);
        bridge.enqueueBatch (plan.description, plan.commands);
        bridge.applyPendingCommands();
    }
}

// ---------------------------------------------------------------------------
TEST_CASE ("IntentEngine: VocalForward raises target volume, one undo reverts", "[ai][phase2][intent]")
{
    EditManager em; em.createNewEdit();
    auto* edit = em.getEdit(); REQUIRE (edit != nullptr);
    AICommandBridge bridge (em);
    IntentEngine engine (em);

    auto tracks = te::getAudioTracks (*edit);
    REQUIRE (! tracks.isEmpty());
    auto* track = tracks[0];

    const float before = te::volumeFaderPositionToDB (track->getVolumePlugin()->volume.get());

    applyPlan (bridge, engine.plan (IntentEngine::Intent::VocalForward, track));

    const float after = te::volumeFaderPositionToDB (track->getVolumePlugin()->volume.get());
    CHECK (after > before + 1.5f);    // ~ +2 dB

    edit->getUndoManager().undo();
    const float reverted = te::volumeFaderPositionToDB (track->getVolumePlugin()->volume.get());
    CHECK (std::abs (reverted - before) < 0.5f);
}

// ---------------------------------------------------------------------------
TEST_CASE ("IntentEngine: Warmer inserts one Cloudes EQ, one undo removes it", "[ai][phase2][intent]")
{
    EditManager em; em.createNewEdit();
    auto* edit = em.getEdit(); REQUIRE (edit != nullptr);
    AICommandBridge bridge (em);
    IntentEngine engine (em);

    auto* track = te::getAudioTracks (*edit)[0];
    const int before = countBuiltIn (track->pluginList, "EQProcessor");

    applyPlan (bridge, engine.plan (IntentEngine::Intent::Warmer, track));
    CHECK (countBuiltIn (track->pluginList, "EQProcessor") == before + 1);

    edit->getUndoManager().undo();
    CHECK (countBuiltIn (track->pluginList, "EQProcessor") == before);
}

// ---------------------------------------------------------------------------
TEST_CASE ("IntentEngine: GlueMix inserts a Cloudes compressor on the master, one undo reverts", "[ai][phase2][intent]")
{
    EditManager em; em.createNewEdit();
    auto* edit = em.getEdit(); REQUIRE (edit != nullptr);
    AICommandBridge bridge (em);
    IntentEngine engine (em);

    const int before = countBuiltIn (edit->getMasterPluginList(), "CompressorProcessor");

    applyPlan (bridge, engine.plan (IntentEngine::Intent::GlueMix, nullptr));
    CHECK (countBuiltIn (edit->getMasterPluginList(), "CompressorProcessor") == before + 1);

    edit->getUndoManager().undo();
    CHECK (countBuiltIn (edit->getMasterPluginList(), "CompressorProcessor") == before);
}

// ---------------------------------------------------------------------------
TEST_CASE ("IntentEngine: category/label/intentsFor mapping is consistent", "[ai][phase2][intent]")
{
    using C = IntentEngine::Category;
    for (auto cat : { C::LevelBalance, C::Character, C::SpaceDepth, C::ReferenceLoudness })
    {
        auto intents = IntentEngine::intentsFor (cat);
        CHECK (intents.size() >= 3);
        for (auto i : intents)
        {
            CHECK (IntentEngine::category (i) == cat);
            CHECK (IntentEngine::label (i).isNotEmpty());
        }
    }
}

// ---------------------------------------------------------------------------
TEST_CASE ("IntentEngine: parsePrompt maps free text to intents (DE/EN, incl. de-ess)", "[ai][phase2][intent]")
{
    using I = IntentEngine::Intent;
    IntentEngine::Intent out;

    REQUIRE (IntentEngine::parsePrompt ("bitte die vocals de-essen", out)); CHECK (out == I::DeEss);
    REQUIRE (IntentEngine::parsePrompt ("zu viel zischen",          out)); CHECK (out == I::DeEss);
    REQUIRE (IntentEngine::parsePrompt ("vocals nach vorn",         out)); CHECK (out == I::VocalForward);
    REQUIRE (IntentEngine::parsePrompt ("make it warmer",           out)); CHECK (out == I::Warmer);
    REQUIRE (IntentEngine::parsePrompt ("more air please",          out)); CHECK (out == I::Clearer);
    REQUIRE (IntentEngine::parsePrompt ("wider stereo",             out)); CHECK (out == I::Wider);
    REQUIRE (IntentEngine::parsePrompt ("louder",                   out)); CHECK (out == I::Louder);
    REQUIRE (IntentEngine::parsePrompt ("glue the mix",             out)); CHECK (out == I::GlueMix);

    CHECK_FALSE (IntentEngine::parsePrompt ("xyzzy nonsense", out));
}

// DeEss intent inserts the De-Esser built-in on the target, one undo reverts.
TEST_CASE ("IntentEngine: DeEss inserts the De-Esser, one undo reverts", "[ai][phase2][intent]")
{
    EditManager em; em.createNewEdit();
    auto* edit = em.getEdit(); REQUIRE (edit != nullptr);
    AICommandBridge bridge (em);
    IntentEngine engine (em);

    auto* track = te::getAudioTracks (*edit)[0];
    REQUIRE (track != nullptr);
    CHECK (countBuiltIn (track->pluginList, "DeEsserProcessor") == 0);

    applyPlan (bridge, engine.plan (IntentEngine::Intent::DeEss, track));
    CHECK (countBuiltIn (track->pluginList, "DeEsserProcessor") == 1);

    em.getUndoManager().undo();
    CHECK (countBuiltIn (track->pluginList, "DeEsserProcessor") == 0);
}
