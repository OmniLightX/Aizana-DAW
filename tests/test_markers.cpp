// test_markers.cpp — Polish sprint (Phase 4 vorlauf): named song markers.
//
// Markers are stored natively in Tracktion Engine via te::MarkerManager on the
// Edit's MarkerTrack. The MarkerLane UI is a pure view over this state, so the
// engine contract is what matters: create / rename / move / delete + single
// undo per gesture + next/prev navigation. All headless.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EditManager.h"

using TimePos = tracktion::core::TimePosition;

namespace
{
    int markerCount (te::Edit& edit)
    {
        return edit.getMarkerManager().getMarkers().size();
    }

    te::MarkerClip* firstMarker (te::Edit& edit)
    {
        auto markers = edit.getMarkerManager().getMarkers();
        return markers.isEmpty() ? nullptr : markers.getFirst().get();
    }
}

TEST_CASE ("MarkerManager: createMarker adds a marker at the given position", "[markers]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    REQUIRE (edit != nullptr);
    REQUIRE (markerCount (*edit) == 0);

    em.getUndoManager().beginNewTransaction ("Add Marker");
    auto clip = edit->getMarkerManager().createMarker (-1, TimePos::fromSeconds (3.0),
                                                       tracktion::TimeDuration(), nullptr);
    REQUIRE (clip != nullptr);
    REQUIRE (markerCount (*edit) == 1);
    REQUIRE_THAT (clip->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (3.0, 0.01));
}

TEST_CASE ("MarkerManager: a created marker is a single undo step", "[markers]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();

    em.getUndoManager().beginNewTransaction ("Add Marker");
    edit->getMarkerManager().createMarker (-1, TimePos::fromSeconds (1.0),
                                           tracktion::TimeDuration(), nullptr);
    REQUIRE (markerCount (*edit) == 1);

    REQUIRE (em.canUndo());
    em.undoLastAction();
    REQUIRE (markerCount (*edit) == 0);
}

TEST_CASE ("MarkerManager: rename, move and delete a marker", "[markers]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    auto& um = em.getUndoManager();

    um.beginNewTransaction ("Add Marker");
    auto clip = edit->getMarkerManager().createMarker (-1, TimePos::fromSeconds (2.0),
                                                       tracktion::TimeDuration(), nullptr);
    REQUIRE (clip != nullptr);

    // Rename
    um.beginNewTransaction ("Rename Marker");
    clip->setName ("Drop");
    REQUIRE (firstMarker (*edit) != nullptr);
    REQUIRE (firstMarker (*edit)->getName() == "Drop");

    // Move (preserveSync=false, keepLength=true — matches MarkerLane drag)
    um.beginNewTransaction ("Move Marker");
    clip->setStart (TimePos::fromSeconds (5.0), false, true);
    REQUIRE_THAT (firstMarker (*edit)->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (5.0, 0.01));

    // Resize the region length (matches the MarkerLane right-edge drag) — markers
    // are stretchable ranges, not just point flags.
    um.beginNewTransaction ("Resize Marker");
    clip->setLength (tracktion::TimeDuration::fromSeconds (4.0), false);
    REQUIRE_THAT (firstMarker (*edit)->getPosition().getLength().inSeconds(),
                  Catch::Matchers::WithinAbs (4.0, 0.01));

    // Delete
    um.beginNewTransaction ("Delete Marker");
    clip->removeFromParent();
    REQUIRE (markerCount (*edit) == 0);
}

TEST_CASE ("MarkerManager: next/prev navigation from a time position", "[markers]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    auto& mm = edit->getMarkerManager();

    em.getUndoManager().beginNewTransaction ("Add Markers");
    mm.createMarker (-1, TimePos::fromSeconds (1.0), tracktion::TimeDuration(), nullptr);
    mm.createMarker (-1, TimePos::fromSeconds (5.0), tracktion::TimeDuration(), nullptr);
    REQUIRE (markerCount (*edit) == 2);

    auto* next = mm.getNextMarker (TimePos::fromSeconds (2.0));
    REQUIRE (next != nullptr);
    REQUIRE_THAT (next->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (5.0, 0.01));

    auto* prev = mm.getPrevMarker (TimePos::fromSeconds (2.0));
    REQUIRE (prev != nullptr);
    REQUIRE_THAT (prev->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (1.0, 0.01));
}
