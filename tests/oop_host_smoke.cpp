// oop_host_smoke.cpp — Phase 5, E5.3b end-to-end smoke test (real cross-process).
//
// One binary, two roles (like the app's scanner worker):
//   * Worker mode  (relaunched with kPluginHostProcessUID on the command line): hosts a stub
//     "Doubler" AudioProcessor via the injected PluginLoader and pumps the SharedAudioRing.
//   * Host mode    (normal launch): creates an OutOfProcessPluginInstance, which spawns THIS
//     binary as the worker, then round-trips a known buffer and checks the doubled result.
//
// This actually exercises the spawn + config handshake + shm round-trip + child processBlock +
// host marshaling + clean shutdown — the parts that can't be covered inside engine_tests.
// Exit 0 = PASS, non-zero = FAIL (CTest).

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugins/oop/OutOfProcessPluginInstance.h"

#include <cmath>
#include <cstdio>

namespace
{
    struct DoublerProcessor : juce::AudioProcessor
    {
        const juce::String getName() const override { return "Doubler"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            b.applyGain (2.0f);
        }
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}
    };
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // gives us a MessageManager for the IPC

    juce::String commandLine;
    for (int i = 1; i < argc; ++i)
        commandLine << (i > 1 ? " " : "") << juce::String (argv[i]);

    cloudes::PluginLoader loader =
        [] (const juce::PluginDescription&, double, int, juce::String&)
            -> std::unique_ptr<juce::AudioProcessor>
        {
            return std::make_unique<DoublerProcessor>();
        };

    // ---- Worker role ----
    if (auto worker = cloudes::startPluginHostWorkerIfRequested (commandLine, loader))
    {
        auto* mm = juce::MessageManager::getInstance();
        for (int guard = 0; ! worker->isFinished() && guard < 400; ++guard)   // ~20s ceiling
            mm->runDispatchLoopUntil (50);
        return 0;
    }

    // ---- Host role ----
    juce::PluginDescription desc;
    desc.name             = "SmokeDoubler";
    desc.pluginFormatName = "Test";
    desc.fileOrIdentifier = "smoke";

    auto proxy = std::make_unique<cloudes::OutOfProcessPluginInstance> (desc, 2);
    proxy->prepareToPlay (44100.0, 64);

    bool pass = proxy->isChildConnected();
    if (! pass)
        std::fprintf (stderr, "OOP smoke: child did not connect\n");

    juce::AudioBuffer<float> buffer (2, 64);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 64; ++i)
            buffer.setSample (c, i, 0.5f);

    juce::MidiBuffer midi;
    proxy->processBlock (buffer, midi);

    for (int c = 0; c < 2 && pass; ++c)
        for (int i = 0; i < 64; ++i)
            if (std::abs (buffer.getSample (c, i) - 1.0f) > 1.0e-4f)   // 0.5 doubled = 1.0
            {
                std::fprintf (stderr, "OOP smoke: sample [%d][%d] = %f, expected 1.0\n",
                              c, i, buffer.getSample (c, i));
                pass = false;
                break;
            }

    proxy->releaseResources();
    proxy.reset();

    std::fprintf (stderr, pass ? "OOP smoke: PASS\n" : "OOP smoke: FAIL\n");
    return pass ? 0 : 1;
}
