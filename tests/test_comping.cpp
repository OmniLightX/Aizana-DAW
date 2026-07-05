// test_comping.cpp — Phase 2+: project-less take comping (CompingEngine)
//
// Covers the Aizana comp model (AIZANACOMP sections in the clip state),
// the lane gestures (applyTakeSection / switchSectionAt), undo behaviour,
// the direct-file take switching fix in ClipEditor::setActiveTake, and the
// offline comp render (each take a distinct DC level → rendered regions
// must carry the level of the take they were comped from).
//
// All tests run headless. Take files are tiny generated WAVs in a temp dir.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EditManager.h"
#include "engine/ClipEditor.h"
#include "engine/CompingEngine.h"

using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;

namespace
{
    constexpr double kTakeSecs   = 2.0;
    constexpr double kSampleRate = 44100.0;

    juce::File makeTempDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_comping_tests")
                       .getNonexistentSibling();
        dir.createDirectory();
        return dir;
    }

    /** Write a mono WAV whose every sample is `level` (distinct per take). */
    juce::File writeConstantWav (const juce::File& dir, const juce::String& name, float level)
    {
        auto file = dir.getChildFile (name);
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), kSampleRate, 1, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();

        juce::AudioBuffer<float> buf (1, (int) (kTakeSecs * kSampleRate));
        for (int i = 0; i < buf.getNumSamples(); ++i)
            buf.setSample (0, i, level);
        REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()));
        return file;
    }

    struct CompFixture
    {
        EditManager em;
        std::unique_ptr<ClipEditor> editor;
        std::unique_ptr<CompingEngine> comping;
        te::WaveAudioClip* clip = nullptr;
        juce::File dir;
        juce::Array<juce::File> takeFiles;

        CompFixture()
        {
            em.createNewEdit();
            editor  = std::make_unique<ClipEditor> (em);
            comping = std::make_unique<CompingEngine> (em);
            comping->attachToEdit();

            auto* track = te::getAudioTracks (*em.getEdit())[0];
            te::ClipPosition pos { TimeRng (TimePos::fromSeconds (0.0),
                                            TimePos::fromSeconds (kTakeSecs)), {} };
            clip = dynamic_cast<te::WaveAudioClip*> (
                track->insertNewClip (te::TrackItem::Type::wave, "comp clip", pos, nullptr));
            REQUIRE (clip != nullptr);

            // Three takes with distinct constant levels 0.1 / 0.2 / 0.3,
            // exactly like project-less loop recording: direct file refs.
            dir = makeTempDir();
            for (int i = 0; i < 3; ++i)
            {
                auto f = writeConstantWav (dir, "take_" + juce::String (i + 1) + ".wav",
                                           0.1f * (float) (i + 1));
                takeFiles.add (f);
                clip->addTake (f);
            }

            // Match the recording flow: the clip's own source is the last take.
            clip->getSourceFileReference().setToDirectFileReference (takeFiles.getLast(), false);
        }

        ~CompFixture()
        {
            dir.deleteRecursively();
        }
    };

    /** Mean sample value of a rendered file inside [t0, t1] (away from fades). */
    float meanLevelBetween (const juce::File& f, double t0, double t1)
    {
        juce::AudioFormatManager afm;
        afm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (f));
        REQUIRE (reader != nullptr);

        const auto a = (juce::int64) (t0 * reader->sampleRate);
        const auto n = (int) ((t1 - t0) * reader->sampleRate);
        juce::AudioBuffer<float> buf (1, n);
        reader->read (&buf, 0, n, a, true, false);

        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += buf.getSample (0, i);
        return (float) (sum / n);
    }
}

// ---------------------------------------------------------------------------
TEST_CASE ("CompingEngine: take files and comp length resolve for direct-file takes", "[comping]")
{
    CompFixture fx;

    auto files = fx.comping->getTakeFiles (*fx.clip);
    REQUIRE (files.size() == 3);
    for (int i = 0; i < 3; ++i)
    {
        CAPTURE (i, files[i].getFullPathName().toStdString(),
                 fx.takeFiles[i].getFullPathName().toStdString());
        REQUIRE (files[i] == fx.takeFiles[i]);
    }

    REQUIRE_THAT (fx.comping->getCompLength (*fx.clip),
                  Catch::Matchers::WithinAbs (kTakeSecs, 0.01));

    // No comp yet; active take = last (clip source points at take 3)
    REQUIRE (! fx.comping->hasComp (*fx.clip));
    REQUIRE (fx.comping->getActiveTakeIndex (*fx.clip) == 2);
}

// ---------------------------------------------------------------------------
TEST_CASE ("CompingEngine: applyTakeSection builds and merges sections", "[comping]")
{
    CompFixture fx;

    // Drag take 0 over [0.5, 1.0] → comp seeded with active take (2) + inserted section
    fx.comping->applyTakeSection (*fx.clip, 0.5, 1.0, 0);
    auto s = fx.comping->getSections (*fx.clip);
    REQUIRE (s.size() == 3);
    REQUIRE (s[0].takeIndex == 2);
    REQUIRE_THAT (s[0].endTime, Catch::Matchers::WithinAbs (0.5, 0.001));
    REQUIRE (s[1].takeIndex == 0);
    REQUIRE_THAT (s[1].endTime, Catch::Matchers::WithinAbs (1.0, 0.001));
    REQUIRE (s[2].takeIndex == 2);
    REQUIRE_THAT (s[2].endTime, Catch::Matchers::WithinAbs (kTakeSecs, 0.001));

    // Overwrite the middle with take 1 over [0.25, 1.5]
    fx.comping->applyTakeSection (*fx.clip, 0.25, 1.5, 1);
    s = fx.comping->getSections (*fx.clip);
    REQUIRE (s.size() == 3);
    REQUIRE (s[0].takeIndex == 2);
    REQUIRE_THAT (s[0].endTime, Catch::Matchers::WithinAbs (0.25, 0.001));
    REQUIRE (s[1].takeIndex == 1);
    REQUIRE_THAT (s[1].endTime, Catch::Matchers::WithinAbs (1.5, 0.001));
    REQUIRE (s[2].takeIndex == 2);

    // Same take adjacent → merged, no zero-length leftovers
    fx.comping->applyTakeSection (*fx.clip, 1.5, kTakeSecs, 1);
    s = fx.comping->getSections (*fx.clip);
    REQUIRE (s.size() == 2);
    REQUIRE (s[1].takeIndex == 1);
    REQUIRE_THAT (s[1].endTime, Catch::Matchers::WithinAbs (kTakeSecs, 0.001));
}

// ---------------------------------------------------------------------------
TEST_CASE ("CompingEngine: switchSectionAt retargets the section under the cursor", "[comping]")
{
    CompFixture fx;

    fx.comping->applyTakeSection (*fx.clip, 0.5, 1.0, 0);   // sections: 2 | 0 | 2
    fx.comping->switchSectionAt (*fx.clip, 0.75, 1);        // middle section → take 1

    auto s = fx.comping->getSections (*fx.clip);
    REQUIRE (s.size() == 3);
    REQUIRE (s[1].takeIndex == 1);
    REQUIRE_THAT (s[1].endTime, Catch::Matchers::WithinAbs (1.0, 0.001));
}

// ---------------------------------------------------------------------------
TEST_CASE ("CompingEngine: comp gesture is one undo step; undo restores previous comp", "[comping][undo]")
{
    CompFixture fx;
    auto& um = fx.em.getUndoManager();

    fx.comping->applyTakeSection (*fx.clip, 0.5, 1.0, 0);
    REQUIRE (fx.comping->getSections (*fx.clip).size() == 3);

    fx.comping->applyTakeSection (*fx.clip, 1.2, 1.8, 1);
    REQUIRE (fx.comping->getSections (*fx.clip).size() == 5);

    um.undo();   // back to the single-gesture comp
    REQUIRE (fx.comping->getSections (*fx.clip).size() == 3);

    um.undo();   // back to no comp at all
    REQUIRE (! fx.comping->hasComp (*fx.clip));
}

// ---------------------------------------------------------------------------
TEST_CASE ("CompingEngine: clearComp removes the comp; selectWholeTake switches the source", "[comping]")
{
    CompFixture fx;

    fx.comping->applyTakeSection (*fx.clip, 0.5, 1.0, 0);
    REQUIRE (fx.comping->hasComp (*fx.clip));

    fx.comping->clearComp (*fx.clip);
    REQUIRE (! fx.comping->hasComp (*fx.clip));

    // Whole-take selection: source string switches, takes stay intact
    fx.comping->selectWholeTake (*fx.clip, 0);
    REQUIRE (fx.comping->getActiveTakeIndex (*fx.clip) == 0);
    REQUIRE (fx.clip->getNumTakes (true) == 3);
}

// ---------------------------------------------------------------------------
TEST_CASE ("ClipEditor: setActiveTake switches direct-file takes WITHOUT deleting them", "[comping][takes]")
{
    CompFixture fx;

    // TE's WaveAudioClip::setCurrentTake would remove the take here (no
    // ProjectItem behind a direct file ref). The ClipEditor fix must not.
    fx.editor->setActiveTake (*fx.clip, 1);

    REQUIRE (fx.clip->getNumTakes (true) == 3);
    REQUIRE (fx.editor->getActiveTake (*fx.clip) == 1);
    REQUIRE (fx.clip->getSourceFileReference().getFile() == fx.takeFiles[1]);

    // Undoable
    fx.em.getUndoManager().undo();
    REQUIRE (fx.editor->getActiveTake (*fx.clip) == 2);
}

// ---------------------------------------------------------------------------
TEST_CASE ("CompingEngine: render mixes the right take into the right region", "[comping][render]")
{
    CompFixture fx;

    // Comp: take 2 (0.3) | take 0 (0.1) on [0.5,1.0] | take 2 (0.3)
    fx.comping->applyTakeSection (*fx.clip, 0.5, 1.0, 0);

    auto rendered = fx.comping->renderCompNow (*fx.clip);
    REQUIRE (rendered.existsAsFile());

    // Sample well inside each region (50ms margin > 10ms crossfade)
    REQUIRE_THAT (meanLevelBetween (rendered, 0.10, 0.40),
                  Catch::Matchers::WithinAbs (0.3, 0.02));
    REQUIRE_THAT (meanLevelBetween (rendered, 0.60, 0.90),
                  Catch::Matchers::WithinAbs (0.1, 0.02));
    REQUIRE_THAT (meanLevelBetween (rendered, 1.10, 1.90),
                  Catch::Matchers::WithinAbs (0.3, 0.02));

    // The clip now plays the rendered comp (TE's transient source override)
    REQUIRE (fx.clip->getCurrentSourceFile() == rendered);

    rendered.deleteFile();
}
