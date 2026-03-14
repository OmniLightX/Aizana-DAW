// test_clip_editing.cpp — AUDIO-03: Non-destructive audio editing
// Status: STUB — implementation added in plan 01-03 (audio engine)
//
// Tests Tracktion Engine clip model: cut, trim, move, copy operations on
// AudioClip instances. All operations must be non-destructive (source file
// unchanged) and undoable via the shared UndoManager.
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Clip: trim start moves clip start position without altering source file", "[clip][AUDIO-03][stub]") {
    SKIP("Stub — implement when Tracktion Engine Edit is available (plan 01-03)");
}

TEST_CASE("Clip: move clip to new timeline position updates TrackItem bounds", "[clip][AUDIO-03][stub]") {
    SKIP("Stub — implement when Tracktion Engine Edit is available (plan 01-03)");
}

TEST_CASE("Clip: copy clip produces independent TrackItem referencing same source", "[clip][AUDIO-03][stub]") {
    SKIP("Stub — implement when Tracktion Engine Edit is available (plan 01-03)");
}
