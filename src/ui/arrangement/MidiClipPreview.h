#pragma once
#include <tracktion_engine/tracktion_engine.h>

namespace aizana
{
// Message-thread view data. Use TE's expanded sequence so loop repetitions and
// content offsets match playback; time conversion also follows the tempo map.
struct MidiPreviewNote { double start, end; int pitch; };
inline std::vector<MidiPreviewNote> midiClipPreview (tracktion::engine::MidiClip& clip)
{
    std::vector<MidiPreviewNote> result;
    const auto range = clip.getPosition().time;
    auto& tempo = clip.edit.tempoSequence;
    for (auto* note : clip.getSequenceLooped().getNotes())
    {
        const auto beat = clip.getContentStartBeat() + tracktion::core::toDuration (note->getStartBeat());
        const auto start = clip.getQuantisation().roundBeatToNearest (beat);
        const auto from = std::max (range.getStart(), tempo.toTime (start));
        const auto to = std::min (range.getEnd(), tempo.toTime (start + note->getLengthBeats()));
        if (to > from)
            result.push_back ({ (from - range.getStart()).inSeconds(),
                                (to - range.getStart()).inSeconds(), note->getNoteNumber() });
    }
    return result;
}
}
