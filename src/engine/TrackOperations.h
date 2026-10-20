#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace aizana::TrackOperations
{
// Message-thread operations. One undo transaction per successful gesture.
bool canDuplicateTrack (const tracktion::engine::AudioTrack&);

// Copies next to the source, including clips, plugins and automation. Media stays
// shared; device recording assignments are deliberately not copied.
tracktion::engine::AudioTrack::Ptr duplicateTrack (tracktion::engine::AudioTrack&);

// Reorders regular sibling tracks without changing their routing or parent.
// Returns false for unsupported tracks, different parents/edits, or a no-op.
bool moveTrackRelative (tracktion::engine::AudioTrack& source,
                        tracktion::engine::AudioTrack& target, bool after);
}
