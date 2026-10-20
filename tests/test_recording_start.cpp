#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <tracktion_engine/tracktion_engine.h>
#include <atomic>
#include <thread>
#include <sys/stat.h>

namespace te = tracktion::engine;
namespace
{
struct TestEngineBehaviour : te::EngineBehaviour
{
    bool autoInitialiseDeviceManager() override { return false; }
    bool addSystemAudioIODeviceTypes() override { return false; }
};
struct RecordingWarnings : te::UIBehaviour
{
    juce::StringArray warnings;
    void showWarningAlert (const juce::String&, const juce::String& message) override
    { warnings.add (message); }
    void showWarningMessage (const juce::String& message) override { warnings.add (message); }
};
struct TestDirectory
{
    juce::File root { juce::File ("/tmp").getChildFile ("aizana-recording-" + juce::Uuid().toString()) };
    TestDirectory() { REQUIRE (root.createDirectory().wasOk()); }
    ~TestDirectory() { root.deleteRecursively(); }
};
struct TestProperties : te::PropertyStorage
{
    juce::File root;
    explicit TestProperties (juce::File folder) : PropertyStorage ("AizanaRecordingRegression"), root (folder) {}
    juce::File getAppCacheFolder() override { return root.getChildFile ("cache"); }
    juce::File getAppPrefsFolder() override { return root.getChildFile ("settings"); }
};
struct RecordingFixture
{
    TestDirectory scratch;
    juce::File directory = scratch.root.getChildFile ("recordings");
    RecordingWarnings* warnings = new RecordingWarnings;
    te::Engine engine { std::make_unique<TestProperties> (scratch.root), std::unique_ptr<te::UIBehaviour> (warnings),
                        std::make_unique<TestEngineBehaviour>() };
    te::HostedAudioDeviceInterface& audio = engine.getDeviceManager().getHostedAudioDeviceInterface();
    std::unique_ptr<te::Edit> edit;
    std::atomic<bool> running { false };
    std::thread audioThread;

    RecordingFixture()
    {
        REQUIRE (directory.createDirectory().wasOk());
        audio.initialise ({});
        engine.getDeviceManager().dispatchPendingUpdates();
        edit = te::Edit::createSingleTrackEdit (engine);
        edit->editFileRetriever = [this] { return directory.getChildFile ("test.tracktionedit"); };
        auto& dm = engine.getDeviceManager();
        auto inputs = dm.getWaveInputDevices();
        REQUIRE (! inputs.empty());
        auto* input = inputs[0];
        input->setEnabled (true);
        dm.dispatchPendingUpdates();
        edit->getTransport().ensureContextAllocated();
        auto* track = te::getAudioTracks (*edit)[0];
        bool assigned = false;
        for (auto* instance : edit->getAllInputDevices())
            if (&instance->getInputDevice() == input)
            {
                REQUIRE (instance->setTarget (track->itemID, true, nullptr).has_value());
                assigned = true;
            }
        REQUIRE (assigned);
        // setTarget schedules context updates. Let them finish, then resolve
        // the current instance again before arming (same contract as TrackHeader).
        pump (50);
        for (auto* instance : edit->getAllInputDevices())
            if (&instance->getInputDevice() == input)
                instance->setRecordingEnabled (track->itemID, true);
        running = true;
        audioThread = std::thread ([this]
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            while (running.load())
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i)
                        buffer.setSample (c, i, 0.1f);
                midi.clear();
                audio.processBlock (buffer, midi);
                std::this_thread::sleep_for (std::chrono::milliseconds (10));
            }
        });
    }
    ~RecordingFixture()
    {
        if (edit != nullptr) edit->getTransport().stop (true, false);
        running = false;
        if (audioThread.joinable()) audioThread.join();
        edit.reset();
        chmod (directory.getFullPathName().toRawUTF8(), 0700);
    }
    void pump (int milliseconds = 150)
    { juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds); }
};
}

TEST_CASE ("Recording start reports preparation failure and preserves transport semantics", "[recording-start]")
{
    juce::MessageManager::getInstance();
    const bool alreadyPlaying = GENERATE (false, true);
    const bool writable = GENERATE (false, true);
    CAPTURE (alreadyPlaying, writable);
    RecordingFixture f;
    auto& transport = f.edit->getTransport();
    f.pump();
    if (alreadyPlaying)
    {
        transport.play (false);
        f.pump();
        REQUIRE (transport.isPlaying());
        REQUIRE (transport.getCurrentPlaybackContext()->getSyncPoint().has_value());
    }
    // Filename mask bypasses default media subdirectory creation, so chmod tests
    // the actual writer destination rather than an unrelated missing directory.
    for (auto* input : f.engine.getDeviceManager().getWaveInputDevices())
        input->setFilenameMask (f.directory.getChildFile ("take_{take}").getFullPathName());
    if (! writable)
    {
        REQUIRE (chmod (f.directory.getFullPathName().toRawUTF8(), 0500) == 0);
        REQUIRE_FALSE (f.directory.hasWriteAccess());
    }
    transport.record (false);
    f.pump (250);
    CHECK (transport.isRecording() == writable);
    CHECK (transport.isPlaying() == (writable || alreadyPlaying));
    CHECK (f.warnings->warnings.isEmpty() == writable);
    if (! writable)
    {
        CHECK (te::getAudioTracks (*f.edit)[0]->getClips().isEmpty());
        CHECK (f.warnings->warnings.joinIntoString (" ").containsIgnoreCase ("write-access"));
        CHECK (f.warnings->warnings.joinIntoString (" ").contains (f.directory.getFullPathName()));
    }
    if (writable)
    {
        transport.stop (false, false);
        f.pump();
        auto clips = te::getAudioTracks (*f.edit)[0]->getClips();
        REQUIRE (! clips.isEmpty());
        auto* clip = dynamic_cast<te::WaveAudioClip*> (clips[0]);
        REQUIRE (clip != nullptr);
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (
            formats.createReaderFor (clip->getSourceFileReference().getFile()));
        REQUIRE (reader != nullptr);
        REQUIRE (reader->lengthInSamples > 0);
        juce::AudioBuffer<float> recorded (1, (int) reader->lengthInSamples);
        REQUIRE (reader->read (&recorded, 0, recorded.getNumSamples(), 0, true, false));
        CHECK (recorded.getMagnitude (0, 0, recorded.getNumSamples()) > 0.01f);
    }
}
