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

TEST_CASE ("Save collects active and alternate takes without changing undo", "[comping][save-media]")
{
    CompFixture fx;
    auto destination = fx.dir.getChildFile ("Saved").getChildFile ("Song.tracktion");
    REQUIRE (destination.getParentDirectory().createDirectory());
    // Recording uses relative references, while imported takes may be absolute.
    fx.clip->getSourceFileReference().setToDirectFileReference (fx.takeFiles[2], true);
    fx.editor->setActiveTake (*fx.clip, 0);
    const auto undoName = fx.em.getUndoManager().getUndoDescription();
    fx.em.saveEdit (destination);
    REQUIRE (destination.existsAsFile());
    CHECK (fx.em.getUndoManager().getUndoDescription() == undoName);
    CHECK (fx.em.getEdit()->editFileRetriever() == destination);
    for (auto file : fx.comping->getTakeFiles (*fx.clip))
    {
        CHECK (file.existsAsFile());
        CHECK (file.isAChildOf (destination.getParentDirectory()));
    }
    fx.em.getUndoManager().undo();
    CHECK (fx.editor->getActiveTake (*fx.clip) == 2);
    CHECK (fx.clip->getSourceFileReference().getFile().existsAsFile());
    fx.em.getUndoManager().redo();
    CHECK (fx.editor->getActiveTake (*fx.clip) == 0);

    EditManager reopened;
    REQUIRE (reopened.loadEdit (destination));
    auto* clip = dynamic_cast<te::WaveAudioClip*> (te::getAudioTracks (*reopened.getEdit())[0]->getClips()[0]);
    REQUIRE (clip != nullptr);
    CompingEngine comps (reopened);
    for (auto file : comps.getTakeFiles (*clip))
    {
        CHECK (file.existsAsFile());
        CHECK (file.isAChildOf (destination.getParentDirectory()));
    }
    CHECK (clip->getSourceFileReference().getFile().existsAsFile());

    // A different user's/root location must not depend on the original absolute
    // recording path. The complete project folder is portable after Save.
    const auto movedFolder = fx.dir.getChildFile ("Moved project");
    REQUIRE (destination.getParentDirectory().copyDirectoryTo (movedFolder));
    EditManager portable;
    REQUIRE (portable.loadEdit (movedFolder.getChildFile (destination.getFileName())));
    auto* moved = dynamic_cast<te::WaveAudioClip*> (te::getAudioTracks (*portable.getEdit())[0]->getClips()[0]);
    REQUIRE (moved != nullptr);
    CHECK (moved->getSourceFileReference().getFile().isAChildOf (movedFolder));
    CompingEngine movedComps (portable);
    for (auto file : movedComps.getTakeFiles (*moved))
    {
        CHECK (file.existsAsFile());
        CHECK (file.isAChildOf (movedFolder));
    }
}

TEST_CASE ("Save As retains an intermediate session recording that only undo references", "[comping][save-media]")
{
    CompFixture fx;
    const auto first = fx.dir.getChildFile ("First/Song.tracktion");
    fx.em.saveEdit (first);
    REQUIRE (first.existsAsFile());
    auto recording = writeConstantWav (fx.em.mediaDirectory(), "Song_Track 1_Take_1.wav", 0.7f);
    auto* track = te::getAudioTracks (*fx.em.getEdit())[0];
    auto* clip = dynamic_cast<te::WaveAudioClip*> (track->insertNewClip (te::TrackItem::Type::wave,
        "recording after first save", { TimeRng (TimePos::fromSeconds (3), TimePos::fromSeconds (5)), {} }, nullptr));
    REQUIRE (clip != nullptr);
    clip->getSourceFileReference().setToDirectFileReference (recording, true);
    const auto descriptor = clip->state[te::IDs::source].toString();
    auto state = clip->state;
    fx.em.getUndoManager().beginNewTransaction ("Delete recording");
    state.getParent().removeChild (state, &fx.em.getUndoManager());
    const auto second = fx.dir.getChildFile ("Second/Song.tracktion");
    fx.em.saveEdit (second);
    REQUIRE (second.existsAsFile());
    REQUIRE (fx.em.getUndoManager().undo());
    const auto restored = te::SourceFileReference::findFileFromString (*fx.em.getEdit(), descriptor);
    CHECK (restored.existsAsFile());
    CHECK (restored.isAChildOf (second.getParentDirectory()));
    CHECK (restored.hasIdenticalContentTo (recording));
    // The old name must be occupied, so the recording allocator cannot reuse
    // a descriptor that still belongs to the historical take.
    CHECK (fx.em.mediaDirectory().getChildFile (recording.getFileName()).existsAsFile());
    const auto next = writeConstantWav (fx.em.mediaDirectory(), "Song_Track 1_Take_2.wav", 0.9f);
    CHECK (te::SourceFileReference::findFileFromString (*fx.em.getEdit(),
        next.getRelativePathFrom (second.getParentDirectory())) == next);
}

TEST_CASE ("Save collects colliding external filenames without overwriting audio", "[comping][save-media]")
{
    CompFixture fx;
    const auto destination = fx.dir.getChildFile ("Collision/Song.tracktion");
    const auto media = aizana::sessionpaths::mediaFolder (destination);
    REQUIRE (media.createDirectory());
    const auto existing = writeConstantWav (media, fx.takeFiles[0].getFileName(), 0.8f);
    fx.em.saveEdit (destination);
    REQUIRE (destination.existsAsFile());
    CHECK_THAT (meanLevelBetween (existing, 0.1, 0.4), Catch::Matchers::WithinAbs (0.8, 0.02));
    const auto takes = fx.comping->getTakeFiles (*fx.clip);
    CHECK (takes[0] != existing);
    CHECK_THAT (meanLevelBetween (takes[0], 0.1, 0.4), Catch::Matchers::WithinAbs (0.1, 0.02));
    fx.em.saveEdit (destination);
    CHECK (fx.comping->getTakeFiles (*fx.clip)[0] == takes[0]);
}

TEST_CASE ("Missing media keeps the last successful save and current identity", "[comping][save-media]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    CompFixture fx;
    const auto destination = fx.dir.getChildFile ("Failure/Song.tracktion");
    fx.em.saveEdit (destination);
    REQUIRE (destination.existsAsFile());
    const auto previous = destination.loadFileAsString();
    const auto missing = fx.comping->getTakeFiles (*fx.clip)[0];
    REQUIRE (missing.deleteFile()); // Only a generated fixture's collected copy.
    fx.em.getEdit()->markAsChanged();
    const auto result = fx.em.saveEdit (destination);
    CHECK (result.failed());
    CHECK (result.getErrorMessage().contains ("Audiodatei fehlt"));
    CHECK (destination.loadFileAsString() == previous);
    CHECK (fx.em.getCurrentFile() == destination);
    CHECK (fx.em.getEdit()->editFileRetriever() == destination);
    CHECK (fx.em.getEdit()->hasChangedSinceSaved());
}

TEST_CASE ("Comp playback survives saving and Save As", "[comping][comp-source]")
{
    CompFixture fx;
    fx.comping->applyTakeSection (*fx.clip, 0.5, 1.0, 0);
    const auto rendered = fx.comping->renderCompNow (*fx.clip);
    REQUIRE (rendered.existsAsFile());
    const auto originalTake = fx.clip->state[te::IDs::source].toString();
    const auto first = fx.dir.getChildFile ("Saved/Song.tracktion");
    REQUIRE (fx.em.saveEdit (first).wasOk());
    CHECK (fx.clip->getCurrentSourceFile() == rendered);
    REQUIRE (fx.em.saveEdit (fx.dir.getChildFile ("Second/Song.tracktion")).wasOk());
    CHECK (fx.clip->getCurrentSourceFile() == rendered);
    CHECK (fx.clip->state[te::IDs::source].toString() == originalTake);
}

TEST_CASE ("Clearing a comp source override restores the selected take", "[comping][comp-source]")
{
    CompFixture fx;
    fx.comping->applyTakeSection (*fx.clip, 0.5, 1.0, 0);
    REQUIRE (fx.comping->renderCompNow (*fx.clip).existsAsFile());
    fx.comping->clearComp (*fx.clip);
    // Exercise the same reset used by the deferred clear/undo handler.
    CompingEngine::setSourceOverride (*fx.clip, {});
    REQUIRE (fx.clip->getCurrentSourceFile() == fx.clip->getSourceFileReference().getFile());
    REQUIRE (fx.clip->getCurrentSourceFile().existsAsFile());
}


struct CompingEngineTestAccess
{
    static void complete (CompingEngine& engine, te::EditItemID id,
                          const juce::File& file, const juce::ValueTree& root,
                          const juce::String& hash)
    {
        engine.applyRenderedFile (id, file, root, hash);
    }
    static juce::String hash (CompingEngine& engine, te::WaveAudioClip& clip)
    {
        return engine.renderRevision (clip);
    }
};

TEST_CASE ("Late comp completion cannot replace a newer comp", "[comping][comp-source]")
{
    CompFixture fx;
    fx.comping->applyTakeSection (*fx.clip, 0.5, 1.0, 0);
    const auto oldFile = fx.comping->renderCompNow (*fx.clip);
    const auto oldHash = CompingEngineTestAccess::hash (*fx.comping, *fx.clip);
    const auto root = fx.em.getEdit()->state;
    fx.comping->applyTakeSection (*fx.clip, 0.5, 1.0, 1);
    const auto newest = fx.comping->renderCompNow (*fx.clip);
    REQUIRE (newest != oldFile);
    CompingEngineTestAccess::complete (*fx.comping, fx.clip->itemID, oldFile, root, oldHash);
    REQUIRE (fx.clip->getCurrentSourceFile() == newest);
}

TEST_CASE ("Comp completion from another edit is ignored even with matching clip ID", "[comping][comp-source]")
{
    CompFixture fx;
    fx.comping->applyTakeSection (*fx.clip, 0.5, 1.0, 0);
    const auto current = fx.comping->renderCompNow (*fx.clip);
    const auto hash = CompingEngineTestAccess::hash (*fx.comping, *fx.clip);
    const auto differentRoot = fx.em.getEdit()->state.createCopy();
    CompingEngineTestAccess::complete (*fx.comping, fx.clip->itemID, fx.takeFiles[0], differentRoot, hash);
    REQUIRE (fx.clip->getCurrentSourceFile() == current);
}

TEST_CASE ("Comp render finishing after Save As still applies unchanged choices", "[comping][comp-source]")
{
    CompFixture fx;
    fx.comping->applyTakeSection (*fx.clip, 0.5, 1.0, 0);
    const auto rendered = fx.comping->renderCompNow (*fx.clip);
    const auto hash = CompingEngineTestAccess::hash (*fx.comping, *fx.clip);
    const auto root = fx.em.getEdit()->state;
    CompingEngine::setSourceOverride (*fx.clip, {});
    REQUIRE (fx.em.saveEdit (fx.dir.getChildFile ("Relocated/Song.tracktion")).wasOk());
    CompingEngineTestAccess::complete (*fx.comping, fx.clip->itemID, rendered, root, hash);
    REQUIRE (fx.clip->getCurrentSourceFile() == rendered);
}

TEST_CASE ("Whole-take comparison preserves the assembled comp", "[comping][comp-preserve]")
{
    CompFixture fx;
    fx.comping->applyTakeSection (*fx.clip, 0.5, 1.0, 0);
    const auto sections = fx.comping->getSections (*fx.clip);
    SECTION ("Take menu") { fx.editor->setActiveTake (*fx.clip, 1); }
    SECTION ("Take lane") { fx.comping->selectWholeTake (*fx.clip, 1); }
    REQUIRE (fx.comping->hasComp (*fx.clip));
    const auto retained = fx.comping->getSections (*fx.clip);
    REQUIRE (retained.size() == sections.size());
    for (int i = 0; i < sections.size(); ++i)
    {
        REQUIRE (retained[i].takeIndex == sections[i].takeIndex);
        REQUIRE (retained[i].endTime == sections[i].endTime);
    }
    REQUIRE (fx.comping->getActiveTakeIndex (*fx.clip) == 1);
}

TEST_CASE ("Comp comparison restores audio and survives undo and session reload", "[comping][comp-preserve]")
{
    CompFixture fx;
    fx.comping->applyTakeSection (*fx.clip, 0.5, 1.0, 0);
    const auto rendered = fx.comping->renderCompNow (*fx.clip);
    REQUIRE (rendered.existsAsFile());
    const auto revision = CompingEngineTestAccess::hash (*fx.comping, *fx.clip);
    fx.em.getUndoManager().clearUndoHistory();
    fx.editor->setActiveTake (*fx.clip, 1);
    REQUIRE_FALSE (CompingEngine::isCompActive (*fx.clip));
    REQUIRE (fx.clip->getCurrentSourceFile() == fx.takeFiles[1]);
    CompingEngineTestAccess::complete (*fx.comping, fx.clip->itemID, rendered,
                                      fx.em.getEdit()->state, revision);
    REQUIRE (fx.clip->getCurrentSourceFile() == fx.takeFiles[1]);
    REQUIRE (fx.em.getUndoManager().undo());
    REQUIRE (CompingEngine::isCompActive (*fx.clip));
    REQUIRE (fx.comping->renderCompNow (*fx.clip) == rendered);
    REQUIRE (fx.em.getUndoManager().redo());
    REQUIRE_FALSE (CompingEngine::isCompActive (*fx.clip));
    const auto destination = fx.dir.getChildFile ("Compare/Song.tracktion");
    REQUIRE (fx.em.saveEdit (destination).wasOk());
    EditManager reopened;
    REQUIRE (reopened.loadEdit (destination));
    auto* clip = dynamic_cast<te::WaveAudioClip*> (te::getAudioTracks (*reopened.getEdit())[0]->getClips()[0]);
    REQUIRE (clip != nullptr);
    CompingEngine comps (reopened);
    comps.attachToEdit();
    REQUIRE (comps.hasComp (*clip));
    REQUIRE_FALSE (CompingEngine::isCompActive (*clip));
    REQUIRE (comps.getSections (*clip).size() == 3);
    REQUIRE (comps.getActiveTakeIndex (*clip) == 1);
    CompingEngine::selectComp (*clip);
    REQUIRE (CompingEngine::isCompActive (*clip));
    const auto restored = comps.renderCompNow (*clip);
    REQUIRE (restored.existsAsFile());
    REQUIRE_THAT (meanLevelBetween (restored, 0.6, 0.9), Catch::Matchers::WithinAbs (0.1, 0.001));
    REQUIRE_THAT (meanLevelBetween (restored, 1.2, 1.7), Catch::Matchers::WithinAbs (0.3, 0.001));
    REQUIRE (reopened.saveEdit (destination).wasOk());
    REQUIRE (clip->getCurrentSourceFile() == restored);
    REQUIRE (reopened.getUndoManager().undo());
    REQUIRE_FALSE (CompingEngine::isCompActive (*clip));
    REQUIRE (reopened.getUndoManager().redo());
    REQUIRE (CompingEngine::isCompActive (*clip));
}
