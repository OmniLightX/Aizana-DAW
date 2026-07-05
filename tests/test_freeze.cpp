// test_freeze.cpp — Polish sprint item 2: Freeze / Bounce-in-Place guard logic.
//
// The freeze ACTION is a thin call to TE's native AudioTrack::setFrozen
// (individualFreeze), which renders the track through its plugin chain to a
// freeze file — too integration-heavy (needs a playback graph + UIBehaviour) to
// exercise headless. What Aizana OWNS is the guard that decides whether Freeze
// is offered: TE refuses to individually-freeze a track routed into another
// track (a bus), so aizana::canFreezeIndividually must agree, and an attempted
// freeze on a routed track must be a no-op (and must NOT trigger a render).

#include <catch2/catch_test_macros.hpp>

#include "engine/EditManager.h"
#include "engine/TrackFreeze.h"

namespace
{
    te::AudioTrack* addAudioTrack (te::Edit& edit)
    {
        edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);
        return te::getAudioTracks (edit).getLast();
    }
}

TEST_CASE ("Freeze: a master-routed track is individually freezable", "[freeze]")
{
    EditManager em;
    em.createNewEdit();
    auto* track = te::getAudioTracks (*em.getEdit()).getFirst();
    REQUIRE (track != nullptr);

    // A fresh track outputs to the device (master) → individually freezable.
    REQUIRE (aizana::canFreezeIndividually (*track));
}

TEST_CASE ("Freeze: a bus-routed track is NOT individually freezable", "[freeze]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();

    auto* src = te::getAudioTracks (*edit).getFirst();
    auto* bus = addAudioTrack (*edit);
    REQUIRE (src != nullptr);
    REQUIRE (bus != nullptr);

    // Route src into bus — the Aizana group-bus pattern.
    src->getOutput().setOutputToTrack (bus);

    REQUIRE_FALSE (aizana::canFreezeIndividually (*src));
    REQUIRE (aizana::canFreezeIndividually (*bus));   // bus → master, freezable

    // TE must refuse the freeze on a routed track (no render triggered):
    // isFrozen stays false.
    src->setFrozen (true, te::Track::individualFreeze);
    REQUIRE_FALSE (src->isFrozen (te::Track::individualFreeze));
}
