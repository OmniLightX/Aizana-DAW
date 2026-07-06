// test_edit_groups.cpp — T1.3: Edit-Gruppen (Studio-One-Stil).
//
// Deckt: Gruppen-Property (undobar), Siblings/Companions-Matching (zeitgleich
// ±1 ms, andere Zeiten NICHT), Split-Propagation (1 Undo für alle Spuren),
// Delete-Propagation (Batch bleibt 1 Undo), Gruppe 0 = keine Propagation.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/EditManager.h"
#include "engine/ClipEditor.h"

using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;

namespace
{
    te::WaveAudioClip* insertClip (te::AudioTrack& track, double startSecs, double lengthSecs)
    {
        te::ClipPosition pos { TimeRng (TimePos::fromSeconds (startSecs),
                                        TimePos::fromSeconds (startSecs + lengthSecs)), {} };
        return dynamic_cast<te::WaveAudioClip*> (
            track.insertNewClip (te::TrackItem::Type::wave, "clip", pos, nullptr));
    }

    struct Fixture
    {
        EditManager em;
        ClipEditor  ce { em };
        te::AudioTrack* t1 = nullptr;
        te::AudioTrack* t2 = nullptr;
        te::AudioTrack* t3 = nullptr;

        Fixture()
        {
            em.createNewEdit();
            auto* edit = em.getEdit();
            edit->ensureNumberOfAudioTracks (3);
            auto tracks = te::getAudioTracks (*edit);
            t1 = tracks[0]; t2 = tracks[1]; t3 = tracks[2];
        }
    };
}

TEST_CASE ("T1.3: setEditGroup persistiert undobar; Siblings korrekt", "[groups][t13]")
{
    Fixture fx;

    CHECK (ClipEditor::getEditGroup (*fx.t1) == 0);
    ClipEditor::setEditGroup (*fx.t1, 2);
    ClipEditor::setEditGroup (*fx.t2, 2);
    ClipEditor::setEditGroup (*fx.t3, 1);   // andere Gruppe

    CHECK (ClipEditor::getEditGroup (*fx.t1) == 2);

    auto sibs = ClipEditor::editGroupSiblings (*fx.t1);
    REQUIRE (sibs.size() == 1);
    CHECK (sibs[0] == fx.t2);

    fx.em.getUndoManager().undo();          // t3-Zuweisung weg
    CHECK (ClipEditor::getEditGroup (*fx.t3) == 0);
}

TEST_CASE ("T1.3: groupCompanions matcht nur ZEITGLEICHE Clips", "[groups][t13]")
{
    Fixture fx;
    ClipEditor::setEditGroup (*fx.t1, 1);
    ClipEditor::setEditGroup (*fx.t2, 1);
    ClipEditor::setEditGroup (*fx.t3, 1);

    auto* c1 = insertClip (*fx.t1, 1.0, 4.0);
    auto* c2 = insertClip (*fx.t2, 1.0, 4.0);        // zeitgleich → Companion
    insertClip (*fx.t3, 2.0, 4.0);                    // versetzt → KEIN Companion
    REQUIRE (c1 != nullptr);
    REQUIRE (c2 != nullptr);

    auto companions = ClipEditor::groupCompanions (*c1);
    REQUIRE (companions.size() == 1);
    CHECK (companions[0] == c2);
}

TEST_CASE ("T1.3: Split propagiert auf die Gruppe — EIN Undo für alle Spuren", "[groups][t13]")
{
    Fixture fx;
    ClipEditor::setEditGroup (*fx.t1, 1);
    ClipEditor::setEditGroup (*fx.t2, 1);

    auto* c1 = insertClip (*fx.t1, 0.0, 4.0);
    auto* c2 = insertClip (*fx.t2, 0.0, 4.0);
    insertClip (*fx.t3, 0.0, 4.0);                    // KEINE Gruppe → unangetastet
    REQUIRE (c1 != nullptr);
    REQUIRE (c2 != nullptr);

    auto* right = fx.ce.cutClip (*c1, 2.0);
    REQUIRE (right != nullptr);

    CHECK (fx.t1->getClips().size() == 2);
    CHECK (fx.t2->getClips().size() == 2);            // Companion mitgeschnitten
    CHECK (fx.t3->getClips().size() == 1);            // ungruppiert bleibt ganz

    // EIN Undo stellt BEIDE Spuren wieder her.
    fx.em.getUndoManager().undo();
    CHECK (fx.t1->getClips().size() == 1);
    CHECK (fx.t2->getClips().size() == 1);
}

TEST_CASE ("T1.3: Delete propagiert auf die Gruppe — Batch bleibt EIN Undo", "[groups][t13]")
{
    Fixture fx;
    ClipEditor::setEditGroup (*fx.t1, 3);
    ClipEditor::setEditGroup (*fx.t2, 3);

    auto* c1 = insertClip (*fx.t1, 0.0, 4.0);
    auto* c2 = insertClip (*fx.t2, 0.0, 4.0);
    REQUIRE (c1 != nullptr);
    REQUIRE (c2 != nullptr);

    fx.ce.deleteClips ({ c1 });                       // nur c1 selektiert

    CHECK (fx.t1->getClips().size() == 0);
    CHECK (fx.t2->getClips().size() == 0);            // Companion mitgelöscht

    fx.em.getUndoManager().undo();                    // EIN Undo → beide zurück
    CHECK (fx.t1->getClips().size() == 1);
    CHECK (fx.t2->getClips().size() == 1);
}

TEST_CASE ("T1.3: ohne Gruppe keine Propagation", "[groups][t13]")
{
    Fixture fx;
    auto* c1 = insertClip (*fx.t1, 0.0, 4.0);
    insertClip (*fx.t2, 0.0, 4.0);
    REQUIRE (c1 != nullptr);

    fx.ce.cutClip (*c1, 2.0);
    CHECK (fx.t1->getClips().size() == 2);
    CHECK (fx.t2->getClips().size() == 1);            // unberührt

    fx.ce.deleteClips ({ fx.t1->getClips()[0] });
    CHECK (fx.t2->getClips().size() == 1);            // unberührt
}
