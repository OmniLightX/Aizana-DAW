// test_auto_tempo.cpp — Phase 3, Task 0: SoundTouch timestretch backend +
// ClipEditor::conformToSongTempo / removeSongTempoConform

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EditManager.h"
#include "engine/ClipEditor.h"

using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;

namespace
{
    constexpr double kSampleRate = 44100.0;
    constexpr double kSourceSecs = 2.0;

    juce::File makeTempDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("cloudes_autotempo_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        REQUIRE (dir.createDirectory());
        return dir;
    }

    /** Writes a 2 s sine WAV — real audio content so AudioFileInfo is valid. */
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

    struct AutoTempoFixture
    {
        EditManager em;
        std::unique_ptr<ClipEditor> editor;
        te::WaveAudioClip* clip = nullptr;
        juce::File dir, wavFile;

        AutoTempoFixture (double songBpm)
        {
            em.createNewEdit();
            editor = std::make_unique<ClipEditor> (em);

            auto* edit = em.getEdit();
            edit->tempoSequence.getTempo (0)->setBpm (songBpm);

            dir = makeTempDir();
            wavFile = writeSineWav (dir, "source.wav");

            auto* track = te::getAudioTracks (*edit)[0];
            te::ClipPosition pos { TimeRng (TimePos::fromSeconds (0.0),
                                            TimePos::fromSeconds (kSourceSecs)), {} };
            clip = dynamic_cast<te::WaveAudioClip*> (
                track->insertNewClip (te::TrackItem::Type::wave, "clip", pos, nullptr));
            REQUIRE (clip != nullptr);
            clip->getSourceFileReference().setToDirectFileReference (wavFile, false);
        }

        ~AutoTempoFixture()
        {
            dir.deleteRecursively();
        }

        double clipLengthSecs() const
        {
            return clip->getPosition().getLength().inSeconds();
        }
    };
}

TEST_CASE ("TimeStretcher: SoundTouch backend is compiled in and initialises", "[autotempo]")
{
    // Without TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1 checkModeIsAvailable
    // falls back to defaultMode (= disabled) and autoTempo would silently no-op.
    REQUIRE (te::TimeStretcher::checkModeIsAvailable (te::TimeStretcher::soundtouchBetter)
             == te::TimeStretcher::soundtouchBetter);
    REQUIRE (te::TimeStretcher::canProcessFor (te::TimeStretcher::soundtouchBetter));
    REQUIRE (te::TimeStretcher::defaultMode == te::TimeStretcher::soundtouchBetter);

    te::TimeStretcher ts;
    ts.initialise (kSampleRate, 512, 2, te::TimeStretcher::soundtouchBetter, {}, false);
    REQUIRE (ts.isInitialised());
}

TEST_CASE ("ClipEditor: conformToSongTempo enables autoTempo + rescales length, 1 undo step",
           "[autotempo]")
{
    // Source material at 100 BPM, song at 120 BPM: the same beats play faster,
    // so the clip shortens by factor 100/120.
    AutoTempoFixture f (120.0);
    REQUIRE_THAT (f.clipLengthSecs(), Catch::Matchers::WithinAbs (kSourceSecs, 0.001));

    REQUIRE (f.editor->conformToSongTempo (*f.clip, 100.0));

    CHECK (f.clip->getAutoTempo());
    CHECK (f.clip->getTimeStretchMode() == te::TimeStretcher::soundtouchBetter);
    REQUIRE_THAT (f.clipLengthSecs(),
                  Catch::Matchers::WithinAbs (kSourceSecs * 100.0 / 120.0, 0.05));

    // Source tempo persisted in the clip's LoopInfo
    const auto info = te::AudioFile (f.em.getEdit()->engine, f.wavFile).getInfo();
    REQUIRE_THAT (f.clip->getLoopInfo().getBpm (info),
                  Catch::Matchers::WithinAbs (100.0, 0.5));

    // ONE undo step reverts the whole gesture: mode, autoTempo, length, loopInfo
    f.em.getUndoManager().undo();
    CHECK_FALSE (f.clip->getAutoTempo());
    REQUIRE_THAT (f.clipLengthSecs(), Catch::Matchers::WithinAbs (kSourceSecs, 0.001));

    f.em.getUndoManager().redo();
    CHECK (f.clip->getAutoTempo());
    REQUIRE_THAT (f.clipLengthSecs(),
                  Catch::Matchers::WithinAbs (kSourceSecs * 100.0 / 120.0, 0.05));
}

TEST_CASE ("ClipEditor: removeSongTempoConform restores natural length", "[autotempo]")
{
    AutoTempoFixture f (120.0);

    // Not conformed yet — nothing to remove
    REQUIRE_FALSE (f.editor->removeSongTempoConform (*f.clip));

    REQUIRE (f.editor->conformToSongTempo (*f.clip, 100.0));
    REQUIRE_THAT (f.clipLengthSecs(),
                  Catch::Matchers::WithinAbs (kSourceSecs * 100.0 / 120.0, 0.05));

    REQUIRE (f.editor->removeSongTempoConform (*f.clip));
    CHECK_FALSE (f.clip->getAutoTempo());
    CHECK (f.clip->getTimeStretchMode() == te::TimeStretcher::disabled);
    REQUIRE_THAT (f.clipLengthSecs(), Catch::Matchers::WithinAbs (kSourceSecs, 0.05));
}

TEST_CASE ("ClipEditor: conformToSongTempo rejects invalid input", "[autotempo]")
{
    AutoTempoFixture f (120.0);

    // BPM outside LoopInfo's accepted range
    REQUIRE_FALSE (f.editor->conformToSongTempo (*f.clip, 10.0));
    REQUIRE_FALSE (f.editor->conformToSongTempo (*f.clip, 2000.0));
    CHECK_FALSE (f.clip->getAutoTempo());

    // Unreadable source file
    f.clip->getSourceFileReference().setToDirectFileReference (
        juce::File ("/nonexistent/missing.wav"), false);
    REQUIRE_FALSE (f.editor->conformToSongTempo (*f.clip, 100.0));
    CHECK_FALSE (f.clip->getAutoTempo());
}
