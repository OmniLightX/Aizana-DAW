// test_automation_curve.cpp — MIX-04: Automation curve draw and edit
//
// Tests Tracktion Engine AutomationCurve on a track parameter.
// The volume parameter (volParam) has range [0.0, 1.0] (fader positions, not dB).
// fader=0.0 corresponds to -inf dB, fader=1.0 corresponds to ~0 dB.
// We use fader position values directly (0.5, 0.25, etc.) for automation points.
//
// Automation values are interpolated correctly at arbitrary playback
// positions (not just at keyframe positions).
//
// All tests run headless (no display, no audio hardware required).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EditManager.h"
#include "engine/AutomationEngine.h"

// Convenience alias
using TimePos = tracktion::core::TimePosition;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static te::AudioTrack* getFirstAudioTrack (te::Edit& edit)
{
    auto tracks = te::getAudioTracks (edit);
    return tracks.isEmpty() ? nullptr : tracks[0];
}

// Parameter ID used for volume automation in VolumeAndPanPlugin (non-master track)
static const juce::String kVolumeParamID = "volume";

// ---------------------------------------------------------------------------
// MIX-04: addAutomationPoint + getValueAtTime at exact point
// ---------------------------------------------------------------------------
TEST_CASE ("AutomationCurve: single point value returned at exact time", "[automation][MIX-04]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    AutomationEngine automation (em);

    auto* track = getFirstAudioTrack (*edit);
    REQUIRE (track != nullptr);

    // Add a point at t=2.0 with fader value 0.5 (within [0,1] range)
    automation.addAutomationPoint (*track, kVolumeParamID, 2.0, 0.5f);

    float v = automation.getValueAtTime (*track, kVolumeParamID, 2.0);
    CHECK_THAT (v, Catch::Matchers::WithinAbs (0.5f, 0.01f));
}

// ---------------------------------------------------------------------------
// MIX-04: Linear interpolation between two keyframes
// ---------------------------------------------------------------------------
TEST_CASE ("AutomationCurve: linear interpolation between keyframes is correct", "[automation][MIX-04]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    AutomationEngine automation (em);

    auto* track = getFirstAudioTrack (*edit);
    REQUIRE (track != nullptr);

    // Points: [t=0, v=1.0], [t=2, v=0.0], [t=4, v=1.0]
    // At t=1.0: linear midpoint between 1.0 and 0.0 = 0.5 ± 0.1
    automation.addAutomationPoint (*track, kVolumeParamID, 0.0, 1.0f);
    automation.addAutomationPoint (*track, kVolumeParamID, 2.0, 0.0f);
    automation.addAutomationPoint (*track, kVolumeParamID, 4.0, 1.0f);

    float v = automation.getValueAtTime (*track, kVolumeParamID, 1.0);
    CHECK_THAT (v, Catch::Matchers::WithinAbs (0.5f, 0.1f));
}

// ---------------------------------------------------------------------------
// MIX-04: getValueAtTime at the second of three keyframes
// ---------------------------------------------------------------------------
TEST_CASE ("AutomationCurve: getValueAtTime returns correct value at keyframe", "[automation][MIX-04]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    AutomationEngine automation (em);

    auto* track = getFirstAudioTrack (*edit);
    REQUIRE (track != nullptr);

    automation.addAutomationPoint (*track, kVolumeParamID, 0.0, 1.0f);
    automation.addAutomationPoint (*track, kVolumeParamID, 2.0, 0.25f);
    automation.addAutomationPoint (*track, kVolumeParamID, 4.0, 1.0f);

    // At t=2.0: exactly on the second keyframe → 0.25
    float v = automation.getValueAtTime (*track, kVolumeParamID, 2.0);
    CHECK_THAT (v, Catch::Matchers::WithinAbs (0.25f, 0.01f));
}

// ---------------------------------------------------------------------------
// MIX-04: clearAutomation removes all points from the curve
// ---------------------------------------------------------------------------
TEST_CASE ("AutomationCurve: clearAutomation removes all points", "[automation][MIX-04]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    AutomationEngine automation (em);

    auto* track = getFirstAudioTrack (*edit);
    REQUIRE (track != nullptr);

    automation.addAutomationPoint (*track, kVolumeParamID, 0.0, 1.0f);
    automation.addAutomationPoint (*track, kVolumeParamID, 2.0, 0.5f);

    // After clearing, no points should remain
    automation.clearAutomation (*track, kVolumeParamID);

    // The curve for the volume param should have 0 points
    auto* vp = track->getVolumePlugin();
    REQUIRE (vp != nullptr);
    auto& curve = vp->volParam->getCurve();
    CHECK (curve.getNumPoints() == 0);
}

// ---------------------------------------------------------------------------
// MIX-04: Automation point addition is individually undoable
// ---------------------------------------------------------------------------
TEST_CASE ("AutomationCurve: addAutomationPoint is individually undoable", "[automation][MIX-04]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    AutomationEngine automation (em);

    auto* track = getFirstAudioTrack (*edit);
    REQUIRE (track != nullptr);

    // Add one point (records one undo step)
    automation.addAutomationPoint (*track, kVolumeParamID, 2.0, 0.5f);

    auto* vp = track->getVolumePlugin();
    REQUIRE (vp != nullptr);
    auto& curve = vp->volParam->getCurve();

    CHECK (curve.getNumPoints() == 1);

    // Undo should remove the point
    em.undoLastAction();
    CHECK (curve.getNumPoints() == 0);
}

// ---------------------------------------------------------------------------
// MIX-04: setMode and getMode round-trip
// ---------------------------------------------------------------------------
TEST_CASE ("AutomationCurve: setMode and getMode round-trip for Read/Write/Touch/Latch", "[automation][MIX-04]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    AutomationEngine automation (em);

    auto* track = getFirstAudioTrack (*edit);
    REQUIRE (track != nullptr);

    automation.setMode (*track, AutomationMode::read);
    CHECK (automation.getMode (*track) == AutomationMode::read);

    automation.setMode (*track, AutomationMode::write);
    CHECK (automation.getMode (*track) == AutomationMode::write);

    automation.setMode (*track, AutomationMode::touch);
    CHECK (automation.getMode (*track) == AutomationMode::touch);

    automation.setMode (*track, AutomationMode::latch);
    CHECK (automation.getMode (*track) == AutomationMode::latch);
}
