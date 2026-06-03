// test_intent_engine.cpp — Phase 2, Block B: IntentEngine
//
// Verifies that intents produce REAL, editable Tracktion-Engine mechanic applied as
// ONE undo transaction via the AICommandBridge:
//   - a level intent changes the target track's volume; one undo reverts it
//   - a character intent inserts an EqualiserPlugin; one undo removes it
//   - a loudness intent inserts a glue CompressorPlugin on the master; one undo reverts
//   - category / label / intentsFor mapping is consistent

#include <catch2/catch_test_macros.hpp>

#include "engine/EditManager.h"
#include "engine/AICommandBridge.h"
#include "ai/IntentEngine.h"

namespace
{
    int countPlugins (te::PluginList& list, const juce::String& xmlType)
    {
        int n = 0;
        for (auto* p : list)
            if (p != nullptr && p->getPluginType() == xmlType)
                ++n;
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
TEST_CASE ("IntentEngine: Warmer inserts one EQ, one undo removes it", "[ai][phase2][intent]")
{
    EditManager em; em.createNewEdit();
    auto* edit = em.getEdit(); REQUIRE (edit != nullptr);
    AICommandBridge bridge (em);
    IntentEngine engine (em);

    auto* track = te::getAudioTracks (*edit)[0];
    const int before = countPlugins (track->pluginList, te::EqualiserPlugin::xmlTypeName);

    applyPlan (bridge, engine.plan (IntentEngine::Intent::Warmer, track));
    CHECK (countPlugins (track->pluginList, te::EqualiserPlugin::xmlTypeName) == before + 1);

    edit->getUndoManager().undo();
    CHECK (countPlugins (track->pluginList, te::EqualiserPlugin::xmlTypeName) == before);
}

// ---------------------------------------------------------------------------
TEST_CASE ("IntentEngine: GlueMix inserts a master compressor, one undo reverts", "[ai][phase2][intent]")
{
    EditManager em; em.createNewEdit();
    auto* edit = em.getEdit(); REQUIRE (edit != nullptr);
    AICommandBridge bridge (em);
    IntentEngine engine (em);

    const int before = countPlugins (edit->getMasterPluginList(), te::CompressorPlugin::xmlTypeName);

    applyPlan (bridge, engine.plan (IntentEngine::Intent::GlueMix, nullptr));
    CHECK (countPlugins (edit->getMasterPluginList(), te::CompressorPlugin::xmlTypeName) == before + 1);

    edit->getUndoManager().undo();
    CHECK (countPlugins (edit->getMasterPluginList(), te::CompressorPlugin::xmlTypeName) == before);
}

// ---------------------------------------------------------------------------
TEST_CASE ("IntentEngine: category/label/intentsFor mapping is consistent", "[ai][phase2][intent]")
{
    using C = IntentEngine::Category;
    for (auto cat : { C::LevelBalance, C::Character, C::SpaceDepth, C::ReferenceLoudness })
    {
        auto intents = IntentEngine::intentsFor (cat);
        CHECK (intents.size() == 3);
        for (auto i : intents)
        {
            CHECK (IntentEngine::category (i) == cat);
            CHECK (IntentEngine::label (i).isNotEmpty());
        }
    }
}
