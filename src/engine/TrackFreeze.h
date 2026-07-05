#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace aizana
{
    /** Whether a track can be individually frozen.

        Tracktion Engine refuses to individually-freeze a track that outputs into
        another track (a bus / submix) — you are meant to freeze the track they
        feed into instead. In Aizana most AI-imported stems are routed to group
        buses, so this guard decides whether the Freeze menu item is offered.

        Mirrors the guard inside AudioTrack::setFrozen(individualFreeze).
    */
    inline bool canFreezeIndividually (tracktion::engine::AudioTrack& track)
    {
        return track.getOutput().getDestinationTrack() == nullptr;
    }
}
