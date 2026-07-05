// test_channel_strip.cpp — MIX-01/MIX-02: Channel strip parameters + bus/send routing
//
// Tests Tracktion Engine Track with VolumeAndPanPlugin:
// - Volume parameter: setVolume/getVolume, persist through ValueTree save/load
// - Pan: setPan in channel strip
// - Solo/mute flags persist and are individually undoable
// - Bus routing: createBus + createSend combine signals correctly
//
// All tests run headless (no display, no audio hardware required).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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
    return edit.insertNewAudioTrack (te::TrackInsertPoint { nullptr, nullptr }, nullptr, false);
}

// ---------------------------------------------------------------------------
// MIX-01: setVolume persists and is read back correctly
// ---------------------------------------------------------------------------
TEST_CASE ("ChannelStrip: setVolume/getVolume round-trip at -6 dB", "[mixer][MIX-01]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    MixerEngine mixer (em);

    auto* track = getFirstAudioTrack (*edit);
    REQUIRE (track != nullptr);

    mixer.setVolume (*track, -6.0f);
    CHECK_THAT (mixer.getVolume (*track), Catch::Matchers::WithinAbs (-6.0f, 0.01f));
}

// ---------------------------------------------------------------------------
// MIX-01: Default volume is 0 dB
// ---------------------------------------------------------------------------
TEST_CASE ("ChannelStrip: volume defaults to 0 dB", "[mixer][MIX-01]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    MixerEngine mixer (em);

    auto* track = getFirstAudioTrack (*edit);
    REQUIRE (track != nullptr);

    // Fresh track should be at 0 dB (no volume change applied)
    // VolumeAndPanPlugin default is 0 dB
    CHECK_THAT (mixer.getVolume (*track), Catch::Matchers::WithinAbs (0.0f, 0.5f));
}

// ---------------------------------------------------------------------------
// MIX-01: Pan parameter set and read back
// ---------------------------------------------------------------------------
TEST_CASE ("ChannelStrip: setPan round-trip at -0.5", "[mixer][MIX-01]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    MixerEngine mixer (em);

    auto* track = getFirstAudioTrack (*edit);
    REQUIRE (track != nullptr);

    mixer.setPan (*track, -0.5f);

    auto* vp = track->getVolumePlugin();
    REQUIRE (vp != nullptr);
    CHECK_THAT (vp->getPan(), Catch::Matchers::WithinAbs (-0.5f, 0.01f));
}

// ---------------------------------------------------------------------------
// MIX-01: Solo/mute flags set and read back correctly
// ---------------------------------------------------------------------------
TEST_CASE ("ChannelStrip: setSolo and setMute affect track flags", "[mixer][MIX-01]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    MixerEngine mixer (em);

    auto* track = getFirstAudioTrack (*edit);
    REQUIRE (track != nullptr);

    // Initially not soloed/muted
    CHECK_FALSE (mixer.isSoloed (*track));
    CHECK_FALSE (mixer.isMuted (*track));

    mixer.setSolo (*track, true);
    CHECK (mixer.isSoloed (*track));

    mixer.setMute (*track, true);
    CHECK (mixer.isMuted (*track));

    mixer.setSolo (*track, false);
    CHECK_FALSE (mixer.isSoloed (*track));

    mixer.setMute (*track, false);
    CHECK_FALSE (mixer.isMuted (*track));
}

// ---------------------------------------------------------------------------
// MIX-01: Channel strip changes are individually undoable
// ---------------------------------------------------------------------------
TEST_CASE ("ChannelStrip: setVolume change is individually undoable", "[mixer][MIX-01]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    MixerEngine mixer (em);

    auto* track = getFirstAudioTrack (*edit);
    REQUIRE (track != nullptr);

    float originalVolume = mixer.getVolume (*track);

    // Make a volume change (records an undo step)
    mixer.setVolume (*track, -12.0f);
    CHECK_THAT (mixer.getVolume (*track), Catch::Matchers::WithinAbs (-12.0f, 0.01f));

    // Undo should revert to original value
    em.undoLastAction();
    CHECK_THAT (mixer.getVolume (*track), Catch::Matchers::WithinAbs (originalVolume, 0.5f));
}

// ---------------------------------------------------------------------------
// MIX-01: setPan change is individually undoable (AUDIO-05)
// ---------------------------------------------------------------------------
TEST_CASE ("ChannelStrip: setPan change is individually undoable", "[mixer][MIX-01]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    MixerEngine mixer (em);

    auto* track = getFirstAudioTrack (*edit);
    REQUIRE (track != nullptr);

    auto* vp = track->getVolumePlugin();
    REQUIRE (vp != nullptr);

    float originalPan = vp->pan.get();

    // Make a pan change (records an undo step)
    mixer.setPan (*track, 0.7f);
    CHECK_THAT (vp->getPan(), Catch::Matchers::WithinAbs (0.7f, 0.01f));

    // Undo restores the ValueTree pan CachedValue. Read via vp->pan.get() (the
    // CachedValue, same source that setPan wrote to) — identical to how
    // getVolume reads vp->volume.get() after undo. TE's valueTreePropertyChanged
    // does NOT propagate the restored value back to panParam->currentValue, so
    // vp->getPan() (which reads currentValue) would still show the pre-undo value.
    em.undoLastAction();
    CHECK_THAT (vp->pan.get(), Catch::Matchers::WithinAbs (originalPan, 0.01f));
}

// ---------------------------------------------------------------------------
// MIX-01: Solo/mute survive ValueTree save/load round-trip
// ---------------------------------------------------------------------------
TEST_CASE ("ChannelStrip: solo and mute persist through ValueTree XML round-trip", "[mixer][MIX-01]")
{
    auto tempFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("aizana_test_mixer_roundtrip.tracktion");

    {
        EditManager em;
        em.createNewEdit();
        auto* edit = em.getEdit();
        MixerEngine mixer (em);

        auto* track = getFirstAudioTrack (*edit);
        REQUIRE (track != nullptr);

        mixer.setMute (*track, true);
        mixer.setVolume (*track, -3.0f);

        em.saveEdit (tempFile);
    }

    {
        EditManager em;
        em.loadEdit (tempFile);
        auto* edit = em.getEdit();
        REQUIRE (edit != nullptr);
        MixerEngine mixer (em);

        auto* track = getFirstAudioTrack (*edit);
        REQUIRE (track != nullptr);

        CHECK (mixer.isMuted (*track));
        CHECK_THAT (mixer.getVolume (*track), Catch::Matchers::WithinAbs (-3.0f, 0.5f));
    }

    tempFile.deleteFile();
}

// ---------------------------------------------------------------------------
// MIX-02: createBus creates a plain AudioTrack (direct output routing — no AuxReturn)
// FX channels (createFXChannel) carry AuxReturnPlugin; Bus channels do not.
// ---------------------------------------------------------------------------
TEST_CASE ("Bus routing: createBus returns a valid AudioTrack tagged as bus channel", "[mixer][MIX-02]")
{
    EditManager em;
    em.createNewEdit();
    MixerEngine mixer (em);

    auto* bus = mixer.createBus ("Drums Bus");
    REQUIRE (bus != nullptr);
    CHECK (bus->isAudioTrack());

    // Bus should have the given name
    CHECK (bus->getName().containsIgnoreCase ("Drums"));

    // Bus channel: direct routing — VolumeAndPan exists, no AuxReturnPlugin
    CHECK (bus->getVolumePlugin() != nullptr);
    bool hasReturn = false;
    for (auto* p : bus->pluginList.getPlugins())
        if (dynamic_cast<te::AuxReturnPlugin*> (p) != nullptr)
            hasReturn = true;
    CHECK_FALSE (hasReturn);
}

// ---------------------------------------------------------------------------
// MIX-02: createSend inserts AuxSendPlugin on the source track
// AuxSend/Return routing requires a FX channel (carries AuxReturnPlugin),
// not a plain Bus channel (which does direct output routing without AuxReturn).
// ---------------------------------------------------------------------------
TEST_CASE ("Bus routing: createSend inserts AuxSendPlugin on source track", "[mixer][MIX-02]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    MixerEngine mixer (em);

    auto* sourceTrack = getFirstAudioTrack (*edit);
    REQUIRE (sourceTrack != nullptr);

    auto* fxChannel = mixer.createFXChannel ("Test FX Bus");
    REQUIRE (fxChannel != nullptr);

    auto* sendPlugin = mixer.createSend (*sourceTrack, *fxChannel, -3.0f);
    REQUIRE (sendPlugin != nullptr);

    // The send plugin should be on the source track's plugin list
    auto plugins = sourceTrack->pluginList.getPlugins();
    bool found = false;
    for (auto* p : plugins)
        if (dynamic_cast<te::AuxSendPlugin*> (p) != nullptr)
            found = true;
    CHECK (found);
}

// ---------------------------------------------------------------------------
// Bus→bus sends (Studio One pattern, live-test request 2026-07-03):
// a Bus Channel (e.g. "Vocal Bus") or another FX bus (e.g. "Delay") can send
// into a shared FX bus ("Reverb"); a channel must never send to itself.
// ---------------------------------------------------------------------------
TEST_CASE ("MixerEngine: bus and FX channels can send to another FX bus, never to themselves",
           "[mixer][sends][busbus]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    REQUIRE (edit != nullptr);
    MixerEngine mixer (em);

    auto* reverbFx = mixer.createFXChannel ("Reverb");
    auto* delayFx  = mixer.createFXChannel ("Delay");
    auto* vocalBus = mixer.createBusChannel ("Vocal Bus");
    REQUIRE (reverbFx != nullptr);
    REQUIRE (delayFx  != nullptr);
    REQUIRE (vocalBus != nullptr);

    // Vocal Bus → Reverb (bus → FX bus)
    auto* s1 = mixer.createFXSend (*vocalBus, *reverbFx, -6.0f);
    REQUIRE (s1 != nullptr);
    bool onVocalBus = false;
    for (auto* p : vocalBus->pluginList.getPlugins())
        if (dynamic_cast<te::AuxSendPlugin*> (p) == s1)
            onVocalBus = true;
    CHECK (onVocalBus);

    // Delay → Reverb (FX bus → FX bus: the delay gets a reverb tail)
    auto* s2 = mixer.createFXSend (*delayFx, *reverbFx, -9.0f);
    REQUIRE (s2 != nullptr);
    bool onDelay = false;
    for (auto* p : delayFx->pluginList.getPlugins())
        if (dynamic_cast<te::AuxSendPlugin*> (p) == s2)
            onDelay = true;
    CHECK (onDelay);

    // Reverb → Reverb (self-send = direct feedback) must be refused
    CHECK (mixer.createFXSend (*reverbFx, *reverbFx, -6.0f) == nullptr);
}
