// test_midi_recording.cpp — AUDIO-04: MIDI record and Piano Roll editing
// Status: STUB — implementation added in plan 01-03 (audio engine)
//
// Tests Tracktion Engine MidiClip + MidiList. A simulated recording session
// must produce the correct note events in the MidiClip. Piano Roll editing
// (note move, velocity edit, quantise) must update the MidiList correctly.
#include <catch2/catch_test_macros.hpp>

TEST_CASE("MidiRecording: simulated note events produce correct MidiList entries", "[midi][AUDIO-04][stub]") {
    SKIP("Stub — implement when MidiEngine is available (plan 01-03)");
}

TEST_CASE("MidiRecording: note velocity editing updates MidiList note correctly", "[midi][AUDIO-04][stub]") {
    SKIP("Stub — implement when MidiEngine is available (plan 01-03)");
}

TEST_CASE("MidiRecording: quantise to grid snaps note positions to nearest 1/16th", "[midi][AUDIO-04][stub]") {
    SKIP("Stub — implement when MidiEngine is available (plan 01-03)");
}
