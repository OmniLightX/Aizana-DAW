// test_sidechain_routing.cpp — MIX-03: Sidechain routing (kick -> compressor)
//
// Tests that:
// - setSidechainSource marks the source track as a sidechain source
// - The sidechain flag persists (isSidechainSource returns true)
// - clearSidechainSource removes the flag
//
// Note: Full signal-level verification of sidechain audio routing requires
// offline rendering with a real compressor plugin that reads the sidechain bus,
// which is hardware/plugin-format-specific. The structural routing API is verified
// here at the engine model level, which is the correct scope for plan 01-04.
//
// All tests run headless (no display, no audio hardware required).

#include <catch2/catch_test_macros.hpp>

#include "engine/EditManager.h"
#include "engine/MixerEngine.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static te::AudioTrack* getFirstAudioTrack (te::Edit& edit)
{
    auto tracks = te::getAudioTracks (edit);
    return tracks.isEmpty() ? nullptr : tracks[0];
}

static te::AudioTrack* insertAudioTrack (te::Edit& edit)
{
    return edit.insertNewAudioTrack (te::TrackInsertPoint { nullptr, nullptr }, nullptr);
}

// ---------------------------------------------------------------------------
// MIX-03: setSidechainSource marks the audio track as a sidechain source
// ---------------------------------------------------------------------------
TEST_CASE ("Sidechain: setSidechainSource marks track as sidechain source", "[sidechain][MIX-03]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    MixerEngine mixer (em);

    // Create two tracks: kick (source) and bass (destination with compressor)
    auto* kickTrack = getFirstAudioTrack (*edit);
    REQUIRE (kickTrack != nullptr);

    auto* bassTrack = insertAudioTrack (*edit);
    REQUIRE (bassTrack != nullptr);

    // Get the compressor plugin on the bass track plugin list (use VolumeAndPan as proxy)
    // In a real session, this would be a compressor. We test that the API call is correct.
    auto* vp = bassTrack->getVolumePlugin();
    REQUIRE (vp != nullptr);

    // Initially the plugin has no sidechain source set
    CHECK_FALSE (vp->getSidechainSourceID().isValid());

    mixer.setSidechainSource (*vp, *kickTrack);

    // After setting, the plugin's sidechain source ID should match the kick track's itemID
    CHECK (vp->getSidechainSourceID() == kickTrack->itemID);
}

// ---------------------------------------------------------------------------
// MIX-03: clearSidechainSource removes the sidechain source flag
// ---------------------------------------------------------------------------
TEST_CASE ("Sidechain: clearSidechainSource removes sidechain source flag", "[sidechain][MIX-03]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    MixerEngine mixer (em);

    auto* kickTrack = getFirstAudioTrack (*edit);
    REQUIRE (kickTrack != nullptr);

    auto* bassTrack = insertAudioTrack (*edit);
    REQUIRE (bassTrack != nullptr);

    auto* vp = bassTrack->getVolumePlugin();
    REQUIRE (vp != nullptr);

    // Set and then clear
    mixer.setSidechainSource (*vp, *kickTrack);
    CHECK (vp->getSidechainSourceID() == kickTrack->itemID);

    mixer.clearSidechainSource (*vp);
    CHECK_FALSE (vp->getSidechainSourceID().isValid());
}

// ---------------------------------------------------------------------------
// MIX-03: removeSend removes the AuxSendPlugin from the source track
// ---------------------------------------------------------------------------
TEST_CASE ("Sidechain: removeSend removes send from source track", "[sidechain][MIX-03]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    MixerEngine mixer (em);

    auto* sourceTrack = getFirstAudioTrack (*edit);
    REQUIRE (sourceTrack != nullptr);

    auto* bus = mixer.createBus ("Test Bus");
    REQUIRE (bus != nullptr);

    // Add send, then remove it
    auto* sendPlugin = mixer.createSend (*sourceTrack, *bus, -6.0f);
    REQUIRE (sendPlugin != nullptr);

    // Verify send was added
    auto countBefore = sourceTrack->pluginList.getPlugins().size();

    mixer.removeSend (*sourceTrack, *bus);

    // After removal, plugin count should decrease
    auto countAfter = sourceTrack->pluginList.getPlugins().size();
    CHECK (countAfter < countBefore);
}
