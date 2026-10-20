#include <catch2/catch_test_macros.hpp>
#include "engine/EditManager.h"
#include "engine/InstrumentTracks.h"
#include "engine/MixerEngine.h"
#include "engine/TrackOperations.h"
#include <set>

namespace ops = aizana::TrackOperations;
using TimePos = tracktion::core::TimePosition;

namespace
{
std::set<juce::String> itemIDs (const juce::ValueTree& state)
{
    std::set<juce::String> ids;
    if (state.hasProperty (te::IDs::id))
        ids.insert (state.getProperty (te::IDs::id).toString());
    for (const auto& child : state)
    {
        auto nested = itemIDs (child);
        ids.insert (nested.begin(), nested.end());
    }
    return ids;
}
}

TEST_CASE ("Track duplication copies independent state, automation and routing", "[track-operations]")
{
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();
    auto* source = te::getAudioTracks (edit)[0];
    source->setName ("Lead");
    aizana::markAsInstrumentTrack (*source);
    source->state.setProperty ("aizanaTrackStereo", false, nullptr);
    source->state.setProperty ("armed", true, nullptr);
    source->setMute (true);
    auto* volume = source->getVolumePlugin();
    REQUIRE (volume != nullptr);
    volume->volParam->getCurve().addPoint (TimePos::fromSeconds (1), 0.25f, 0.0f);
    te::ClipPosition position { { TimePos::fromSeconds (0), TimePos::fromSeconds (4) }, {} };
    REQUIRE (source->insertNewClip (te::TrackItem::Type::midi, "Notes", position, nullptr) != nullptr);
    auto bus = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);
    bus->state.setProperty (kChannelTypeProp, kChannelTypeBus, nullptr);
    source->getOutput().setOutputToTrack (bus.get());
    auto originalIDs = itemIDs (source->state);
    const int before = te::getAudioTracks (edit).size();
    edit.getUndoManager().clearUndoHistory();

    auto copy = ops::duplicateTrack (*source);
    REQUIRE (copy != nullptr);
    auto copyID = copy->itemID;
    CHECK (te::getAudioTracks (edit)[1] == copy.get());
    CHECK (copy->getName() == "Lead Kopie");
    CHECK (aizana::isInstrumentTrack (*copy));
    CHECK_FALSE ((bool) copy->state.getProperty ("aizanaTrackStereo"));
    CHECK_FALSE ((bool) copy->state.getProperty ("armed"));
    CHECK ((bool) source->state.getProperty ("armed"));
    CHECK (copy->isMuted (false));
    CHECK (copy->getOutput().getDestinationTrack() == bus.get());
    CHECK (source->getOutput().getDestinationTrack() == bus.get());
    REQUIRE (copy->getClips().size() == 1);
    CHECK (copy->getClips()[0]->getName() == "Notes");
    CHECK (copy->pluginList.size() == source->pluginList.size());
    REQUIRE (copy->getVolumePlugin() != nullptr);
    CHECK (copy->getVolumePlugin()->volParam->getCurve().getNumPoints() == 1);
    for (const auto& id : itemIDs (copy->state))
        CHECK (originalIDs.count (id) == 0);
    for (auto* input : edit.getAllInputDevices())
        CHECK_FALSE (te::isOnTargetTrack (*input, *copy, 0));

    CHECK (edit.getUndoManager().undo());
    CHECK (te::getAudioTracks (edit).size() == before);
    CHECK (te::findAudioTrackForID (edit, copyID) == nullptr);
    copy = nullptr;
    CHECK (edit.getUndoManager().redo());
    auto* restored = te::findAudioTrackForID (edit, copyID);
    REQUIRE (restored != nullptr);
    CHECK (restored->getOutput().getDestinationTrack() == bus.get());
    restored->getVolumePlugin()->volParam->getCurve().addPoint (TimePos::fromSeconds (2), 0.75f, 0.0f);
    CHECK (source->getVolumePlugin()->volParam->getCurve().getNumPoints() == 1);

    const auto busID = bus->itemID;
    bus = nullptr;
    juce::TemporaryFile file (".tracktionedit");
    REQUIRE (em.saveEdit (file.getFile()).wasOk());
    REQUIRE (em.loadEdit (file.getFile()));
    auto* loaded = te::findAudioTrackForID (*em.getEdit(), copyID);
    REQUIRE (loaded != nullptr);
    CHECK (loaded->getName() == "Lead Kopie");
    CHECK (loaded->getClips().size() == 1);
    CHECK (loaded->getVolumePlugin()->volParam->getCurve().getNumPoints() == 2);
    REQUIRE (loaded->getOutput().getDestinationTrack() != nullptr);
    CHECK (loaded->getOutput().getDestinationTrack()->itemID == busID);
}

TEST_CASE ("Track reorder handles first and last positions with undo and rejects invalid targets", "[track-operations]")
{
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();
    auto* first = te::getAudioTracks (edit)[0];
    auto second = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);
    auto third = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);
    auto bus = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);
    bus->state.setProperty (kChannelTypeProp, kChannelTypeBus, nullptr);
    first->getOutput().setOutputToTrack (bus.get());
    auto parent = first->state.getParent();
    const int firstIndex = parent.indexOf (first->state);
    edit.getUndoManager().clearUndoHistory();

    REQUIRE (ops::moveTrackRelative (*third, *first, false));
    CHECK (parent.indexOf (third->state) == firstIndex);
    CHECK (first->getOutput().getDestinationTrack() == bus.get());
    REQUIRE (edit.getUndoManager().undo());
    CHECK (parent.indexOf (first->state) == firstIndex);
    REQUIRE (edit.getUndoManager().redo());
    CHECK (parent.indexOf (third->state) == firstIndex);
    REQUIRE (ops::moveTrackRelative (*third, *second, true));
    CHECK (parent.indexOf (third->state) == parent.indexOf (second->state) + 1);
    CHECK_FALSE (ops::moveTrackRelative (*third, *second, true));
    CHECK_FALSE (ops::moveTrackRelative (*first, *first, false));
    CHECK_FALSE (ops::moveTrackRelative (*first, *bus, false));
    CHECK (ops::duplicateTrack (*bus) == nullptr);

    for (auto tag : { kChannelTypeFX, kChannelTypeVCA, kChannelTypeCue })
    {
        bus->state.setProperty (kChannelTypeProp, tag, nullptr);
        CHECK_FALSE (ops::canDuplicateTrack (*bus));
        CHECK (ops::duplicateTrack (*bus) == nullptr);
    }
}

TEST_CASE ("Audio track duplicate shares source media through save and reload", "[track-operations]")
{
    juce::TemporaryFile media (".wav");
    juce::TemporaryFile session (".tracktionedit");
    {
        juce::WavAudioFormat format;
        auto stream = media.getFile().createOutputStream();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            format.createWriterFor (stream.get(), 44100, 1, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        juce::AudioBuffer<float> samples (1, 4410);
        samples.clear();
        REQUIRE (writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples()));
    }
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();
    auto* source = te::getAudioTracks (edit)[0];
    te::ClipPosition position { { TimePos::fromSeconds (1), TimePos::fromSeconds (1.1) }, {} };
    auto clip = source->insertWaveClip ("Audio", media.getFile(), position, false);
    REQUIRE (clip != nullptr);
    clip->getSourceFileReference().setToDirectFileReference (media.getFile(), false);
    auto copy = ops::duplicateTrack (*source);
    REQUIRE (copy != nullptr);
    REQUIRE (copy->getClips().size() == 1);
    auto* copiedClip = dynamic_cast<te::WaveAudioClip*> (copy->getClips()[0]);
    REQUIRE (copiedClip != nullptr);
    CHECK (copiedClip->itemID != clip->itemID);
    CHECK (copiedClip->getSourceFileReference().getFile() == media.getFile());
    const auto copyID = copy->itemID;
    const auto sourceID = source->itemID;
    REQUIRE (ops::moveTrackRelative (*copy, *source, false));
    copy = nullptr;
    clip = nullptr;
    REQUIRE (em.saveEdit (session.getFile()).wasOk());
    REQUIRE (em.loadEdit (session.getFile()));
    auto* loaded = te::findAudioTrackForID (*em.getEdit(), copyID);
    REQUIRE (loaded != nullptr);
    REQUIRE (loaded->getClips().size() == 1);
    CHECK (loaded->getClips()[0]->getSourceFileReference().getFile().existsAsFile());
    auto* original = te::findAudioTrackForID (*em.getEdit(), sourceID);
    REQUIRE (original != nullptr);
    CHECK (loaded->getClips()[0]->getSourceFileReference().getFile()
           == original->getClips()[0]->getSourceFileReference().getFile());
    CHECK (te::getAudioTracks (*em.getEdit())[0] == loaded);
}

TEST_CASE ("Track operations preserve all bus destinations and EQ state", "[track-operations]")
{
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();
    auto* source = te::getAudioTracks (edit)[0];
    auto other = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);
    auto bus = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);
    bus->state.setProperty (kChannelTypeProp, kChannelTypeBus, nullptr);
    source->getOutput().setOutputToTrack (bus.get());
    other->getOutput().setOutputToTrack (bus.get());
    auto plugin = edit.getPluginCache().createNewPlugin (te::EqualiserPlugin::xmlTypeName, juce::PluginDescription{});
    auto* eq = dynamic_cast<te::EqualiserPlugin*> (plugin.get());
    REQUIRE (eq != nullptr);
    source->pluginList.insertPlugin (plugin, -1, nullptr);
    eq->setLowGain (-6.0f);
    eq->setHighFreq (7300.0f);
    eq->loGain->getCurve().addPoint (TimePos::fromSeconds (0), -6.0f, 0.0f);
    eq->loGain->getCurve().addPoint (TimePos::fromSeconds (1), 3.0f, 0.0f);
    auto copy = ops::duplicateTrack (*source);
    REQUIRE (copy != nullptr);
    auto findEQ = [] (te::AudioTrack& t) -> te::EqualiserPlugin*
    {
        for (auto* p : t.pluginList)
            if (auto* found = dynamic_cast<te::EqualiserPlugin*> (p))
                return found;
        return nullptr;
    };
    auto* copiedEQ = findEQ (*copy);
    REQUIRE (copiedEQ != nullptr);
    CHECK (copiedEQ != eq);
    CHECK (copiedEQ->itemID != eq->itemID);
    CHECK (copiedEQ->hiFreqValue.get() == 7300.0f);
    CHECK (copiedEQ->loGain->getCurve().getNumPoints() == 2);
    CHECK (copiedEQ->loGain->getCurve().getPointValue (1) == 3.0f);
    copiedEQ->loGain->getCurve().setPointValue (1, 9.0f);
    CHECK (eq->loGain->getCurve().getPointValue (1) == 3.0f);
    const auto sourceID = source->itemID;
    const auto otherID = other->itemID;
    const auto copyID = copy->itemID;
    const auto busID = bus->itemID;
    auto checkRouting = [&] (te::Edit& current)
    {
        for (auto id : { sourceID, otherID, copyID })
        {
            auto* t = te::findAudioTrackForID (current, id);
            REQUIRE (t != nullptr);
            REQUIRE (t->getOutput().getDestinationTrack() != nullptr);
            CHECK (t->getOutput().getDestinationTrack()->itemID == busID);
        }
    };
    checkRouting (edit);
    REQUIRE (ops::moveTrackRelative (*other, *source, false));
    checkRouting (edit);
    REQUIRE (edit.getUndoManager().undo());
    checkRouting (edit);
    REQUIRE (edit.getUndoManager().redo());
    checkRouting (edit);
    juce::TemporaryFile file (".tracktionedit");
    REQUIRE (em.saveEdit (file.getFile()).wasOk());
    // No reference-counted objects from an old edit may outlive the edit swap.
    plugin = nullptr;
    other = nullptr;
    bus = nullptr;
    copy = nullptr;
    REQUIRE (em.loadEdit (file.getFile()));
    checkRouting (*em.getEdit());
    auto* loadedEQ = findEQ (*te::findAudioTrackForID (*em.getEdit(), copyID));
    REQUIRE (loadedEQ != nullptr);
    CHECK (loadedEQ->hiFreqValue.get() == 7300.0f);
    CHECK (loadedEQ->loGain->getCurve().getPointValue (1) == 9.0f);
}
