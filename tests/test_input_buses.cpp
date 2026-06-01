// test_input_buses.cpp — Phase 2: Named Software Input Buses (InputBusManager)
//
// Tests the ValueTree model layer of Named Buses — the part that is independent
// of audio hardware:
//   - CRUD: create / rename / remap / delete, with correct ordering + re-indexing
//   - indexOfBusNamed lookup
//   - undoability (each mutation is one transaction)
//   - persistence: <INPUTBUSES> lives under edit->state and serialises with the session
//
// resolveBusToDevice / applyToEngine require real WaveInputDevices and are
// exercised at runtime, not here. All tests run headless (no audio hardware).

#include <catch2/catch_test_macros.hpp>

#include "engine/EditManager.h"
#include "engine/InputBusManager.h"

// ---------------------------------------------------------------------------
TEST_CASE ("InputBusManager: a fresh edit has no buses", "[buses][phase2]")
{
    EditManager em;
    em.createNewEdit();
    InputBusManager buses (em);

    CHECK (buses.getNumBuses() == 0);
    CHECK (buses.getBuses().isEmpty());
}

// ---------------------------------------------------------------------------
TEST_CASE ("InputBusManager: createBus stores name, channel and width", "[buses][phase2]")
{
    EditManager em;
    em.createNewEdit();
    InputBusManager buses (em);

    buses.createBus ("Vocal Mic", 2, false);

    REQUIRE (buses.getNumBuses() == 1);
    const auto b = buses.getBus (0);
    CHECK (b.name == "Vocal Mic");
    CHECK (b.hwChannel == 2);
    CHECK (b.stereo == false);
    CHECK (b.isValid());
}

// ---------------------------------------------------------------------------
TEST_CASE ("InputBusManager: buses keep creation order", "[buses][phase2]")
{
    EditManager em;
    em.createNewEdit();
    InputBusManager buses (em);

    buses.createBus ("Mic 1",   0, false);
    buses.createBus ("Guitar",  4, false);
    buses.createBus ("Drum OH", 6, true);

    REQUIRE (buses.getNumBuses() == 3);
    CHECK (buses.getBus (0).name == "Mic 1");
    CHECK (buses.getBus (1).name == "Guitar");
    CHECK (buses.getBus (2).name == "Drum OH");
    CHECK (buses.getBus (2).stereo == true);
}

// ---------------------------------------------------------------------------
TEST_CASE ("InputBusManager: renameBus updates only the target bus", "[buses][phase2]")
{
    EditManager em;
    em.createNewEdit();
    InputBusManager buses (em);

    buses.createBus ("A", 0, false);
    buses.createBus ("B", 1, false);

    buses.renameBus (1, "Bass DI");

    CHECK (buses.getBus (0).name == "A");
    CHECK (buses.getBus (1).name == "Bass DI");
}

// ---------------------------------------------------------------------------
TEST_CASE ("InputBusManager: remapBus updates channel and stereo flag", "[buses][phase2]")
{
    EditManager em;
    em.createNewEdit();
    InputBusManager buses (em);

    buses.createBus ("OH", 0, false);
    buses.remapBus (0, 6, true);

    CHECK (buses.getBus (0).hwChannel == 6);
    CHECK (buses.getBus (0).stereo == true);
}

// ---------------------------------------------------------------------------
TEST_CASE ("InputBusManager: deleteBus removes and re-indexes", "[buses][phase2]")
{
    EditManager em;
    em.createNewEdit();
    InputBusManager buses (em);

    buses.createBus ("A", 0, false);
    buses.createBus ("B", 1, false);
    buses.createBus ("C", 2, false);

    buses.deleteBus (1);  // remove "B"

    REQUIRE (buses.getNumBuses() == 2);
    CHECK (buses.getBus (0).name == "A");
    CHECK (buses.getBus (1).name == "C");
}

// ---------------------------------------------------------------------------
TEST_CASE ("InputBusManager: indexOfBusNamed finds buses and returns -1 when absent",
           "[buses][phase2]")
{
    EditManager em;
    em.createNewEdit();
    InputBusManager buses (em);

    buses.createBus ("Vocal Mic", 0, false);
    buses.createBus ("Guitar DI", 4, false);

    CHECK (buses.indexOfBusNamed ("Guitar DI") == 1);
    CHECK (buses.indexOfBusNamed ("Nonexistent") == -1);
}

// ---------------------------------------------------------------------------
TEST_CASE ("InputBusManager: createBus is undoable", "[buses][phase2]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    REQUIRE (edit != nullptr);
    InputBusManager buses (em);

    buses.createBus ("Temp", 3, false);
    REQUIRE (buses.getNumBuses() == 1);

    edit->getUndoManager().undo();
    CHECK (buses.getNumBuses() == 0);
}

// ---------------------------------------------------------------------------
TEST_CASE ("InputBusManager: buses serialise with the edit state", "[buses][phase2]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    REQUIRE (edit != nullptr);
    InputBusManager buses (em);

    buses.createBus ("Vocal Mic", 2, false);

    // <INPUTBUSES> must be a child of edit->state so it round-trips in the .session XML.
    const auto xml = edit->state.toXmlString();
    CHECK (xml.contains ("INPUTBUSES"));
    CHECK (xml.contains ("Vocal Mic"));
}
