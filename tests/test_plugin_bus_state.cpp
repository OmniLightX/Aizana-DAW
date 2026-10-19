#include <catch2/catch_test_macros.hpp>
#include "engine/EditManager.h"
#include "plugins/BuiltInPluginFormat.h"

TEST_CASE ("Plugin layout metadata removal and malformed restoration preserve processor buses",
           "[bus-layout]")
{
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();
    auto plugin = edit.getPluginCache().createNewPlugin (
        te::ExternalPlugin::xmlTypeName,
        BuiltInPluginFormat::getDescriptionFor ("Aizana Compressor"));
    auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin.get());
    REQUIRE (ext != nullptr);
    te::getAudioTracks (edit)[0]->pluginList.insertPlugin (plugin, -1, nullptr);
    auto* processor = ext->getAudioPluginInstance();
    REQUIRE (processor != nullptr);
    const auto original = processor->getBusesLayout();
    REQUIRE (original.inputBuses.size() == 2); // main + sidechain
    REQUIRE (original.outputBuses.size() == 1);
    ext->flushPluginStateToValueTree();
    REQUIRE (plugin->state.hasProperty (te::IDs::layout));
    auto& um = edit.getUndoManager();
    um.clearUndoHistory();
    um.beginNewTransaction ("Change stored layout");
    SECTION ("undo can remove derived layout metadata")
    {
        plugin->state.removeProperty (te::IDs::layout, &um);
    }
    SECTION ("old or malformed presets cannot change bus topology")
    {
        juce::MemoryBlock bytes;
        juce::MemoryOutputStream output (bytes, false);
        juce::ValueTree ("LAYOUT").writeToStream (output);
        plugin->state.setProperty (te::IDs::layout, juce::var (bytes), &um);
    }
    CHECK (processor->getBusesLayout() == original);
    REQUIRE (um.undo());
    CHECK (processor->getBusesLayout() == original);
    REQUIRE (um.redo());
    CHECK (processor->getBusesLayout() == original);
}

TEST_CASE ("First track insertion undo preserves session infrastructure across repeated redo",
           "[track-undo-structure]")
{
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();
    auto& um = edit.getUndoManager();
    const auto initialCount = te::getAudioTracks (edit).size();
    um.clearUndoHistory();
    um.beginNewTransaction ("Insert audio track");
    auto track = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);
    REQUIRE (track != nullptr);
    const auto id = track->itemID;
    for (int i = 0; i < 3; ++i)
    {
        REQUIRE (um.undo());
        CHECK (te::getAudioTracks (edit).size() == initialCount);
        REQUIRE (um.redo());
        auto tracks = te::getAudioTracks (edit);
        REQUIRE (tracks.size() == initialCount + 1);
        CHECK (tracks.getLast()->itemID == id);
        CHECK (tracks.getLast()->getVolumePlugin() != nullptr);
        CHECK (tracks.getLast()->getLevelMeterPlugin() != nullptr);
    }
}

namespace
{
class ResizableBusProcessor final : public juce::AudioPluginInstance
{
public:
    ResizableBusProcessor() : AudioPluginInstance (BusesProperties()
        .withInput ("Main", juce::AudioChannelSet::stereo(), true)
        .withInput ("Side", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Main", juce::AudioChannelSet::stereo(), true)) {}
    const juce::String getName() const override { return "Resizable test processor"; }
    void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = getName(); }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    bool canAddBus (bool) const override { return true; }
    bool canRemoveBus (bool) const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};
}
TEST_CASE ("Stored layouts validate before changing dynamically removable plugin buses", "[bus-layout]")
{
    EditManager em; em.createNewEdit();
    auto& edit = *em.getEdit();
    edit.engine.getPluginManager().createPluginInstance =
        [] (const juce::PluginDescription&, double, int, juce::String&)
        { return std::make_unique<ResizableBusProcessor>(); };
    auto plugin = edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName,
        BuiltInPluginFormat::getDescriptionFor ("Aizana Compressor"));
    auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin.get());
    REQUIRE (ext != nullptr);
    auto* processor = ext->getAudioPluginInstance();
    REQUIRE (processor != nullptr);
    const auto original = processor->getBusesLayout();
    REQUIRE (original.inputBuses.size() == 2);
    REQUIRE (original.outputBuses.size() == 1);
    ext->flushPluginStateToValueTree();
    auto& um = edit.getUndoManager();
    um.clearUndoHistory();
    um.beginNewTransaction ("Restore layout");
    bool validReduction = false;
    juce::ValueTree malformed ("LAYOUT");
    SECTION ("valid saved layout can remove and restore a sidechain bus")
    {
        validReduction = true;
        for (auto name : { "INPUTS", "OUTPUTS" })
        {
            juce::ValueTree direction (name), bus ("BUS");
            bus.setProperty ("index", 0, nullptr);
            bus.setProperty ("layout", "L R", nullptr);
            direction.addChild (bus, -1, nullptr);
            malformed.addChild (direction, -1, nullptr);
        }
    }
    SECTION ("empty layout") {}
    SECTION ("incomplete bus declaration")
    {
        juce::ValueTree inputs ("INPUTS"), bus ("BUS");
        bus.setProperty ("index", 3, nullptr);
        inputs.addChild (bus, -1, nullptr);
        malformed.addChild (inputs, -1, nullptr);
    }
    juce::MemoryBlock bytes;
    juce::MemoryOutputStream output (bytes, false);
    malformed.writeToStream (output);
    plugin->state.setProperty (te::IDs::layout, juce::var (bytes), &um);
    if (validReduction)
    {
        CHECK (processor->getBusCount (true) == 1);
        CHECK (processor->getBusCount (false) == 1);
        REQUIRE (um.undo());
        CHECK (processor->getBusesLayout() == original);
        REQUIRE (um.redo());
        CHECK (processor->getBusCount (true) == 1);
        CHECK (processor->getBusCount (false) == 1);
    }
    else
        CHECK (processor->getBusesLayout() == original);
}
