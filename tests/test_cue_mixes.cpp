// test_cue_mixes.cpp — P6: Cue Channels (Kopfhörer-Mixes fürs Tracking)
//
// Ein Cue-Kanal ist ein AuxReturn-Kanal wie ein FX-Channel, aber:
//   - eigener Tag kChannelTypeCue (eigene Strip-Farbe, Output-Selector)
//   - Sends in den Cue sind PRE-FADER per Default (Kopfhörer-Mix folgt
//     nicht den Main-Mix-Fader-Moves)
//   - Output zeigt auf ein wählbares Hardware-Ausgangspaar statt Master

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/EditManager.h"
#include "engine/MixerEngine.h"

using Catch::Approx;

TEST_CASE ("MixerEngine: cue channel creation and classification", "[cue][p6][mixer]")
{
    EditManager em;
    em.createNewEdit();
    MixerEngine mixer (em);

    auto* edit = em.getEdit();
    const int tracksBefore = te::getAudioTracks (*edit).size();

    auto* cue = mixer.createCueChannel ("Cue 1");
    REQUIRE (cue != nullptr);

    // Classification: cue is cue — and nothing else
    REQUIRE (MixerEngine::isCueChannel (*cue));
    REQUIRE_FALSE (MixerEngine::isAudioChannel (*cue));   // hidden from arrangement/AI
    REQUIRE_FALSE (MixerEngine::isFXChannel (*cue));
    REQUIRE_FALSE (MixerEngine::isBusChannel (*cue));
    REQUIRE_FALSE (MixerEngine::isVCAChannel (*cue));

    // Query lists stay disjoint
    REQUIRE (mixer.getCueChannels().size() == 1);
    REQUIRE (mixer.getCueChannels()[0] == cue);
    REQUIRE (mixer.getFXChannels().isEmpty());
    REQUIRE (mixer.getBusChannels().isEmpty());

    // Appended at END (track-number invariant)
    auto tracks = te::getAudioTracks (*edit);
    REQUIRE (tracks.size() == tracksBefore + 1);
    REQUIRE (tracks.getLast() == cue);

    // AuxReturn present so sends can target it; fader plugin exists
    bool hasReturn = false;
    for (auto* p : cue->pluginList.getPlugins())
        if (dynamic_cast<te::AuxReturnPlugin*> (p) != nullptr)
            hasReturn = true;
    REQUIRE (hasReturn);
    REQUIRE (cue->getVolumePlugin() != nullptr);

    // Not solo-isolated (solo on other tracks must mute the cue return)
    REQUIRE_FALSE (cue->isSoloIsolate (false));
}

TEST_CASE ("MixerEngine: cue and FX channels get unique bus numbers", "[cue][p6][mixer]")
{
    EditManager em;
    em.createNewEdit();
    MixerEngine mixer (em);

    auto* fx1  = mixer.createFXChannel ("FX 1");
    auto* cue  = mixer.createCueChannel ("Cue 1");
    auto* fx2  = mixer.createFXChannel ("FX 2");
    REQUIRE (fx1 != nullptr);
    REQUIRE (cue != nullptr);
    REQUIRE (fx2 != nullptr);

    auto busNumOf = [] (te::AudioTrack* t) -> int
    {
        for (auto* p : t->pluginList.getPlugins())
            if (auto* ret = dynamic_cast<te::AuxReturnPlugin*> (p))
                return ret->busNumber.get();
        return -1;
    };

    const int b1 = busNumOf (fx1);
    const int b2 = busNumOf (cue);
    const int b3 = busNumOf (fx2);
    REQUIRE (b1 >= 0);
    REQUIRE (b2 >= 0);
    REQUIRE (b3 >= 0);
    REQUIRE (b1 != b2);
    REQUIRE (b2 != b3);
    REQUIRE (b1 != b3);
}

TEST_CASE ("MixerEngine: sends into a cue default to pre-fader", "[cue][p6][mixer]")
{
    EditManager em;
    em.createNewEdit();
    MixerEngine mixer (em);

    auto* edit   = em.getEdit();
    auto* source = te::getAudioTracks (*edit)[0];
    REQUIRE (source != nullptr);

    auto* cue = mixer.createCueChannel ("Cue 1");
    auto* fx  = mixer.createFXChannel ("FX 1");
    REQUIRE (cue != nullptr);
    REQUIRE (fx != nullptr);

    // Cue send: created + PRE-fader (headphone mix independent of main fader)
    auto* cueSend = mixer.createFXSend (*source, *cue, -6.0f);
    REQUIRE (cueSend != nullptr);
    REQUIRE (mixer.isFXSendPreFader (*source, *cue));

    // FX send keeps the classic post-fader default
    auto* fxSend = mixer.createFXSend (*source, *fx, -6.0f);
    REQUIRE (fxSend != nullptr);
    REQUIRE_FALSE (mixer.isFXSendPreFader (*source, *fx));

    // Send level/enable APIs work against the cue target (generic bus plumbing)
    mixer.setFXSendLevel (*source, *cue, -12.0f);
    REQUIRE (mixer.getFXSendLevel (*source, *cue) == Approx (-12.0f).margin (0.01));
    REQUIRE (mixer.isFXSendEnabled (*source, *cue));
    mixer.setFXSendEnabled (*source, *cue, false);
    REQUIRE_FALSE (mixer.isFXSendEnabled (*source, *cue));

    // Self-send stays blocked (feedback guard)
    REQUIRE (mixer.createFXSend (*cue, *cue, -6.0f) == nullptr);

    // Sends to a plain audio track stay blocked (guard extends, not weakens)
    auto* plain = te::getAudioTracks (*edit)[0];
    REQUIRE (mixer.createFXSend (*cue, *plain, -6.0f) == nullptr);
}

TEST_CASE ("MixerEngine: cue output device assignment is undoable", "[cue][p6][mixer]")
{
    EditManager em;
    em.createNewEdit();
    MixerEngine mixer (em);

    auto* cue = mixer.createCueChannel ("Cue 1");
    REQUIRE (cue != nullptr);

    // Fresh cue outputs to the default device (audible immediately)
    REQUIRE (cue->getOutput().usesDefaultAudioOut());

    // Headless: no real devices — TrackOutput stores the raw ID string, which
    // is exactly what getOutputDeviceID() returns when unresolvable.
    mixer.setCueOutputDeviceID (*cue, "phones-3-4");
    REQUIRE (cue->getOutput().getOutputDeviceID() == "phones-3-4");
    REQUIRE_FALSE (cue->getOutput().usesDefaultAudioOut());

    // Undo restores the default output (TrackOutput writes through the UM)
    em.getUndoManager().undo();
    REQUIRE (cue->getOutput().usesDefaultAudioOut());
    em.getUndoManager().redo();
    REQUIRE (cue->getOutput().getOutputDeviceID() == "phones-3-4");

    // Empty ID = back to default, also undoable
    mixer.setCueOutputDeviceID (*cue, {});
    REQUIRE (cue->getOutput().usesDefaultAudioOut());
    em.getUndoManager().undo();
    REQUIRE (cue->getOutput().getOutputDeviceID() == "phones-3-4");

    // Guard: no-op on non-cue channels
    auto* fx = mixer.createFXChannel ("FX 1");
    mixer.setCueOutputDeviceID (*fx, "phones-3-4");
    REQUIRE (fx->getOutput().usesDefaultAudioOut());
}
