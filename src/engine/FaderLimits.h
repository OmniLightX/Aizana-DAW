#pragma once

namespace aizana
{
    // Tracktion VolumeAndPan clamps its normalised slider to [0, 1].
    // Position 1 corresponds to +6 dB. Keep previews within that range.
    inline constexpr double kMaxFaderGainDb = 6.0;
}
