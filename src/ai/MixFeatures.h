#pragma once

#include "MixAction.h"   // (pulls juce_core); TrackMixProfile lives in MixingEngine.h
#include <vector>

namespace aizana
{
    struct TrackMixProfile;   // fwd (defined in MixingEngine.h)

    /**
        MixFeatures — the feature contract between the heuristic MixingEngine and the
        local ML backend (Phase 4 / Slice 5a). This is the stable layout a trained
        model is exported against: change the order/Count here and you must retrain.

        Phase 4 / Slice 5a ships ONE ML touch-point: a gain-trim refinement. The model
        receives `buildGainTrim()` and returns a single value — a suggested trim in dB
        on top of the heuristic gain. The engine clamps it hard (±kMlTrimClampDb) and
        only applies it to moves the heuristic already decided, so an unavailable or
        misbehaving model degrades exactly to the pure heuristic. More decisions move
        behind the backend as real models mature; each gets its own feature builder.

        Pure data + a pure builder — no engine, no Tracktion, no I/O. Deliberately
        decoupled from MixingEngine::Role (group is compared as a string) to avoid an
        include cycle.
    */
    namespace mixfeatures
    {
        // Gain-trim feature layout. Indices are part of the model contract.
        enum GainTrimIndex
        {
            RmsNorm = 0,   // (rmsDb + 60) / 60, clamped 0..1  — how hot the source is
            PeakNorm,      // (peakDb + 60) / 60, clamped 0..1
            IsVocal,       // 1 if group == "Vocals"
            IsDrums,       // 1 if group == "Drums"
            IsBass,        // 1 if group == "Bass"
            IsLead,        // 1 if this is the lead vocal
            GainTrimCount  // == input length the gain-trim model expects
        };

        /** Output convention for the gain-trim model: out[0] = suggested gain trim in
            dB (the engine clamps to ±MixingEngine::kMlTrimClampDb). */
        std::vector<float> buildGainTrim (const TrackMixProfile& t);
    }
}
