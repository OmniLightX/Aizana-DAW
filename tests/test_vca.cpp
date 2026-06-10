// test_vca.cpp — Phase 2+: VCA channels (group faders, no audio path)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/EditManager.h"
#include "engine/MixerEngine.h"

using Catch::Approx;

TEST_CASE ("MixerEngine: VCA channel create / assign / unassign", "[vca][mixer]")
{
    EditManager em;
    em.createNewEdit();
    MixerEngine mixer (em);

    auto* edit = em.getEdit();
    auto* member = te::getAudioTracks (*edit)[0];
    REQUIRE (member != nullptr);

    auto* vca = mixer.createVCAChannel ("VCA 1");
    REQUIRE (vca != nullptr);
    REQUIRE (MixerEngine::isVCAChannel (*vca));
    REQUIRE_FALSE (MixerEngine::isAudioChannel (*vca));   // hidden from arrangement
    REQUIRE_FALSE (MixerEngine::isBusChannel (*vca));

    // Assign — membership stored on the member track, resolvable both ways
    mixer.assignTrackToVCA (*member, *vca);
    REQUIRE (mixer.getVCAFor (*member) == vca);
    REQUIRE (mixer.getVCAMembers (*vca).size() == 1);
    REQUIRE (mixer.getVCAMembers (*vca)[0] == member);

    // Assignment is undoable
    em.getUndoManager().undo();
    REQUIRE (mixer.getVCAFor (*member) == nullptr);
    em.getUndoManager().redo();
    REQUIRE (mixer.getVCAFor (*member) == vca);

    // Unassign
    mixer.unassignFromVCA (*member);
    REQUIRE (mixer.getVCAFor (*member) == nullptr);
    REQUIRE (mixer.getVCAMembers (*vca).isEmpty());

    // A VCA never claims members of another VCA
    auto* vca2 = mixer.createVCAChannel ("VCA 2");
    mixer.assignTrackToVCA (*member, *vca2);
    REQUIRE (mixer.getVCAMembers (*vca).isEmpty());
    REQUIRE (mixer.getVCAMembers (*vca2).size() == 1);

    // VCA list query
    REQUIRE (mixer.getVCAChannels().size() == 2);
}

TEST_CASE ("MixerEngine: VCA fader position persists via VolumeAndPanPlugin", "[vca][mixer]")
{
    EditManager em;
    em.createNewEdit();
    MixerEngine mixer (em);

    auto* vca = mixer.createVCAChannel ("VCA 1");
    REQUIRE (vca != nullptr);
    REQUIRE (vca->getVolumePlugin() != nullptr);

    mixer.setVolume (*vca, -12.0f);
    REQUIRE (mixer.getVolume (*vca) == Approx (-12.0f).margin (0.2));
}
