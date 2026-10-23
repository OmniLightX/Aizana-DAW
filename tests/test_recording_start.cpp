#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/AizanaEngineBehaviour.h"
#include "engine/EditManager.h"
#include "ui/arrangement/LiveRecordingWaveform.h"
#include <atomic>
#include <thread>
#include <sys/stat.h>

namespace te = tracktion::engine;
namespace
{
struct TestEngineBehaviour : AizanaEngineBehaviour
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
        REQUIRE (directory.getChildFile ("test.tracktionedit").replaceWithText ("<EDIT/>"));
        edit->editFileRetriever = [this] { return directory.getChildFile ("test.tracktionedit"); };
        edit->filePathResolver = [this] (const juce::String& path)
        {
            return juce::File::isAbsolutePath (path) ? juce::File (path) : directory.getChildFile (path);
        };
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

    template <typename Predicate>
    bool waitUntil (Predicate ready, int timeoutMs = 15000)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMs);
        while (! ready())
        {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            pump (20);
        }
        return true;
    }
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


TEST_CASE ("Production recording policy mutes previous clips", "[recording-monitor]")
{
    EditManager manager;
    CHECK (manager.getEngine().getEngineBehaviour().muteTrackContentsWhilstRecording());
}

TEST_CASE ("Recording clip muting follows the capture window", "[recording-monitor]")
{
    const int mode = GENERATE (0, 1, 2);
    CAPTURE (mode);
    const bool punch = mode == 1;
    const bool looping = mode == 2;
    RecordingFixture f;
    auto* track = te::getAudioTracks (*f.edit)[0];
    auto other = f.edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*f.edit), nullptr);
    REQUIRE (other);
    auto muted = [&] (te::AudioTrack& t)
    {
        for (auto* input : f.edit->getAllInputDevices())
            if (input->shouldTrackContentsBeMuted (t)) return true;
        return false;
    };
    CHECK_FALSE (muted (*track));
    auto& transport = f.edit->getTransport();
    if (mode == 0)
    {
        transport.record (false);
        f.pump (700);
        transport.stop (false, false);
        f.pump();
        REQUIRE_FALSE (track->getClips().isEmpty());
        CHECK (AizanaEngineBehaviour::recordingPriority (*track->getClips()[0]) > 0);
    }
    f.edit->recordingPunchInOut = punch;
    transport.setLoopRange ({ tracktion::core::TimePosition::fromSeconds (0.5),
                             tracktion::core::TimePosition::fromSeconds (looping ? 3.5 : 1.5) });
    transport.looping = looping;
    transport.setPosition (tracktion::core::TimePosition::fromSeconds (0));
    transport.record (false);
    f.pump (120);
    REQUIRE (transport.isRecording());
    CHECK (muted (*track) == ! punch);
    CHECK_FALSE (muted (*other));
    // Hosted audio advances by processed samples, not message-loop wall time.
    REQUIRE (f.waitUntil ([&] { return transport.getPosition().inSeconds() >= 0.8; }));
    CHECK (muted (*track));
    CHECK_FALSE (muted (*other));
    if (looping)
    {
        for (int i = 0; i < 220; ++i)
        {
            f.pump (30);
            CHECK (muted (*track));
            CHECK_FALSE (muted (*other));
        }
    }
    if (punch)
    {
        REQUIRE (f.waitUntil ([&] { return transport.getPosition().inSeconds() >= 1.8; }));
        CHECK_FALSE (muted (*track));
    }
    transport.stop (false, false);
    f.pump();
    CHECK_FALSE (muted (*track));
}

TEST_CASE ("Live recording waveform is visible before stop and follows scrolling", "[recording-waveform]")
{
    juce::MessageManager::getInstance();
    const int mode = GENERATE (0, 1, 2);
    CAPTURE (mode);
    RecordingFixture f;
    f.pump();
    auto* track = te::getAudioTracks (*f.edit)[0];
    auto& transport = f.edit->getTransport();
    transport.setPosition (tracktion::core::TimePosition::fromSeconds (0));
    transport.looping = mode == 2;
    f.edit->recordingPunchInOut = mode == 1;
    transport.setLoopRange ({ tracktion::core::TimePosition::fromSeconds (0.5),
                             tracktion::core::TimePosition::fromSeconds (3.0) });
    transport.record (false);
    if (mode == 2)
    {
        // Observe an actual loop wrap; a fixed sleep may not process a full
        // pass on a busy hosted runner.
        double previous = 0.0;
        REQUIRE (f.waitUntil ([&]
        {
            const double now = transport.getPosition().inSeconds();
            const bool wrapped = previous > 2.5 && now < previous;
            previous = now;
            return wrapped;
        }));
    }
    // Also leave visible material after the loop boundary, rather than testing
    // a zero-width instant at the exact wrap point.
    REQUIRE (f.waitUntil ([&] { return transport.getPosition().inSeconds() >= 0.8; }));
    REQUIRE (transport.isRecording());
    REQUIRE (f.waitUntil ([&]
    {
        for (auto* input : f.edit->getEditInputDevices().getDevicesForTargetTrack (*track))
        {
            if (! input->isRecording (track->itemID)) continue;
            const auto file = input->getRecordingFile (track->itemID);
            if (file == juce::File()) continue;
            if (f.engine.getRecordingThumbnailManager().getThumbnailFor (file)->thumb->getTotalLength() > 0.2)
                return true;
        }
        return false;
    }));
    juce::Image image (juce::Image::ARGB, 400, 80, true);
    juce::Graphics g (image);
    INFO ("position=" << transport.getPosition().inSeconds());
    for (auto* input : f.edit->getEditInputDevices().getDevicesForTargetTrack (*track))
    {
        const auto file = input->getRecordingFile (track->itemID);
        INFO ("recording=" << input->isRecording (track->itemID) << " file=" << file.getFullPathName().toStdString());
        if (! input->isRecording (track->itemID)) continue;
        REQUIRE (file != juce::File());
        auto thumb = f.engine.getRecordingThumbnailManager().getThumbnailFor (file);
        INFO ("length=" << thumb->thumb->getTotalLength() << " origin=" << thumb->punchInTime.inSeconds());
        REQUIRE (thumb->thumb->getTotalLength() > 0.0);
    }
    REQUIRE (LiveRecordingWaveform::draw (g, *track, image.getBounds(), 200.0, 0.0));
    // The waveform colour must exist, not merely a coloured recording box.
    int waveformPixels = 0;
    for (int y = 3; y < 77; ++y)
        for (int x = 3; x < 397; ++x)
        {
            const auto pixel = image.getPixelAt (x, y);
            if (pixel.getRed() > 100 && pixel.getGreen() > 180 && pixel.getBlue() > 180)
                ++waveformPixels;
        }
    CHECK (waveformPixels > 0);
    CHECK (LiveRecordingWaveform::draw (g, *track, image.getBounds(), 200.0, 40.0));
    CHECK_FALSE (LiveRecordingWaveform::draw (g, *track, image.getBounds(), 200.0, 10000.0));
    transport.stop (false, false);
    f.pump();
    CHECK_FALSE (LiveRecordingWaveform::draw (g, *track, image.getBounds(), 200.0, 0.0));
    CHECK_FALSE (track->getClips().isEmpty());
}
