// test_clip_merge.cpp — Editing-Tools-Welle: ClipEditor::mergeClips
// (S1 "Events verbinden" / Logic "Join", Cmd+J).
//
// (1) RENDER-BEWEIS Audio: zwei Events mit Lücke → EIN Clip über die Union-Range,
//     beide Events hörbar in der neuen Datei, die Lücke ist Stille; EIN Undo
//     stellt beide Original-Clips wieder her.
// (2) MIDI: Notenlisten beider Clips landen korrekt versetzt in EINEM Clip
//     (TE mergeMidiClips), EIN Undo stellt beide Clips wieder her.
// (3) Guards: verschiedene Spuren / <2 Clips / gemischte Typen → nullptr,
//     kein State-Schaden.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/EditManager.h"
#include "engine/ClipEditor.h"

#include <cmath>

using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;
using BeatPos = tracktion::core::BeatPosition;
using BeatDur = tracktion::core::BeatDuration;

namespace
{
    constexpr double kSampleRate = 44100.0;
    constexpr double kSourceSecs = 1.0;

    juce::File writeSineWav (const juce::File& dir, const juce::String& name)
    {
        auto file = dir.getChildFile (name);
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), kSampleRate, 1, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();

        juce::AudioBuffer<float> buf (1, (int) (kSourceSecs * kSampleRate));
        for (int i = 0; i < buf.getNumSamples(); ++i)
            buf.setSample (0, i, 0.5f * std::sin (2.0 * juce::MathConstants<double>::pi
                                                  * 440.0 * i / kSampleRate));
        REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()));
        return file;
    }

    // RMS eines Zeitfensters der Datei — beweist wo Audio liegt und wo Stille ist.
    float segmentRms (const juce::File& file, double t0, double t1)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        REQUIRE (reader != nullptr);

        const auto start = (juce::int64) (t0 * reader->sampleRate);
        const int   num  = (int) ((t1 - t0) * reader->sampleRate);
        REQUIRE (num > 0);
        REQUIRE (start + num <= reader->lengthInSamples);

        juce::AudioBuffer<float> buf ((int) reader->numChannels, num);
        reader->read (&buf, 0, num, start, true, true);
        return buf.getRMSLevel (0, 0, num);
    }

    te::MidiClip* insertMidiClip (te::AudioTrack& track, double startSecs, double lengthSecs)
    {
        te::ClipPosition pos { TimeRng (TimePos::fromSeconds (startSecs),
                                        TimePos::fromSeconds (startSecs + lengthSecs)), {} };
        return dynamic_cast<te::MidiClip*> (
            track.insertNewClip (te::TrackItem::Type::midi, "m", pos, nullptr));
    }
}

TEST_CASE ("MergeClips: two audio events with a gap become ONE rendered clip", "[merge]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_merge_test");
    tempDir.createDirectory();
    const auto srcA = writeSineWav (tempDir, "a.wav");
    const auto srcB = writeSineWav (tempDir, "b.wav");

    auto* track = te::getAudioTracks (*edit)[0];
    auto* clipA = clipEditor.importAudioFile (srcA, *track, 1.0);   // 1.0–2.0 s
    auto* clipB = clipEditor.importAudioFile (srcB, *track, 3.0);   // 3.0–4.0 s
    REQUIRE (clipA != nullptr);
    REQUIRE (clipB != nullptr);

    auto* merged = clipEditor.mergeClips ({ clipA, clipB });
    REQUIRE (merged != nullptr);

    // ONE clip spanning the union range 1.0–4.0 s
    REQUIRE (track->getClips().size() == 1);
    CHECK (merged->getPosition().getStart().inSeconds() == Catch::Approx (1.0));
    CHECK (merged->getPosition().getEnd().inSeconds()   == Catch::Approx (4.0));

    // RENDER-BEWEIS: file time 0–1 s = event A, 1–2 s = the gap (silence),
    // 2–3 s = event B.
    const auto mergedFile = merged->getSourceFileReference().getFile();
    REQUIRE (mergedFile.existsAsFile());
    CHECK (mergedFile != srcA);
    CHECK (segmentRms (mergedFile, 0.1, 0.9) > 0.1f);
    CHECK (segmentRms (mergedFile, 1.2, 1.8) < 0.02f);
    CHECK (segmentRms (mergedFile, 2.1, 2.9) > 0.1f);

    // EIN Undo stellt beide Original-Events wieder her
    em.getUndoManager().undo();
    REQUIRE (track->getClips().size() == 2);
    CHECK (track->getClips()[0]->getSourceFileReference().getFile() == srcA);
    CHECK (track->getClips()[1]->getSourceFileReference().getFile() == srcB);

    tempDir.deleteRecursively();
}

TEST_CASE ("MergeClips: two MIDI clips combine into one, notes keep their positions", "[merge]")
{
    EditManager em;
    em.createNewEdit();
    auto* track = te::getAudioTracks (*em.getEdit())[0];
    REQUIRE (track != nullptr);
    ClipEditor clipEditor (em);
    auto& um = em.getUndoManager();

    // Default 120 BPM: 1 beat = 0.5 s. Clip 1 at 0–2 s (beat 0), clip 2 at 2–4 s (beat 4).
    auto* c1 = insertMidiClip (*track, 0.0, 2.0);
    auto* c2 = insertMidiClip (*track, 2.0, 2.0);
    REQUIRE (c1 != nullptr);
    REQUIRE (c2 != nullptr);

    c1->getSequence().addNote (60, BeatPos::fromBeats (0.0), BeatDur::fromBeats (0.5), 100, 0, &um);
    c1->getSequence().addNote (62, BeatPos::fromBeats (1.0), BeatDur::fromBeats (0.5), 100, 0, &um);
    c2->getSequence().addNote (64, BeatPos::fromBeats (0.5), BeatDur::fromBeats (0.5), 100, 0, &um);

    auto* merged = clipEditor.mergeClips ({ c1, c2 });
    REQUIRE (merged != nullptr);
    REQUIRE (track->getClips().size() == 1);

    auto* mergedMidi = dynamic_cast<te::MidiClip*> (merged);
    REQUIRE (mergedMidi != nullptr);
    CHECK (merged->getPosition().getStart().inSeconds() == Catch::Approx (0.0));
    CHECK (merged->getPosition().getEnd().inSeconds()   == Catch::Approx (4.0));

    // Clip 2's note (relative beat 0.5, clip start beat 4) lands at merged beat 4.5.
    juce::Array<double> starts;
    for (auto* n : mergedMidi->getSequence().getNotes())
        starts.add (n->getStartBeat().inBeats());
    REQUIRE (starts.size() == 3);
    CHECK (starts.contains (0.0));
    CHECK (starts.contains (1.0));
    CHECK (starts.contains (4.5));

    // EIN Undo stellt beide MIDI-Clips wieder her
    um.undo();
    CHECK (track->getClips().size() == 2);
}

TEST_CASE ("MergeClips: guards — different tracks, <2 clips, mixed types", "[merge]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_merge_guard_test");
    tempDir.createDirectory();
    const auto srcA = writeSineWav (tempDir, "a.wav");

    // A fresh edit has one audio track — add a second for the cross-track guard (I-06).
    edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    auto tracks = te::getAudioTracks (*edit);
    REQUIRE (tracks.size() >= 2);
    auto* t0 = tracks[0];
    auto* t1 = tracks[1];

    auto* wave0 = clipEditor.importAudioFile (srcA, *t0, 0.0);
    auto* wave1 = clipEditor.importAudioFile (srcA, *t1, 0.0);
    auto* midi0 = insertMidiClip (*t0, 2.0, 2.0);
    REQUIRE (wave0 != nullptr);
    REQUIRE (wave1 != nullptr);
    REQUIRE (midi0 != nullptr);

    // Different tracks → rejected
    CHECK (clipEditor.mergeClips ({ wave0, wave1 }) == nullptr);
    // Fewer than 2 clips → rejected
    CHECK (clipEditor.mergeClips ({ wave0 }) == nullptr);
    // Mixed audio+MIDI on the same track → rejected
    CHECK (clipEditor.mergeClips ({ wave0, midi0 }) == nullptr);

    // Nothing was merged or removed
    CHECK (t0->getClips().size() == 2);
    CHECK (t1->getClips().size() == 1);

    tempDir.deleteRecursively();
}
