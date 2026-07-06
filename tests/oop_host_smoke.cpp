// oop_host_smoke.cpp — Phase 5, E5.3b/E5.4 end-to-end smoke test (real cross-process).
//
// One binary, two roles (like the app's scanner worker):
//   * Worker mode  (relaunched with kPluginHostProcessUID on the command line): hosts a stub
//     gain processor via the injected PluginLoader and pumps the SharedAudioRing.
//   * Host mode    (normal launch): creates an OutOfProcessPluginInstance, which spawns THIS
//     binary as the worker, then verifies:
//       E5.3b — spawn + config handshake + shm round-trip + child processBlock + marshaling
//       E5.4  — parameter metadata across the boundary, host param proxy → child effect,
//               MIDI in/out through the ring, getState/setState round trip
// Exit 0 = PASS, non-zero = FAIL (CTest).

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugins/oop/OutOfProcessPluginInstance.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    // Stub plugin: output = input * 2 * gain (gain = one automatable parameter, default 1),
    // echoes all input MIDI, state = the raw gain float.
    struct GainStubProcessor : juce::AudioProcessor
    {
        juce::AudioParameterFloat* gain = nullptr;

        GainStubProcessor()
        {
            addParameter (gain = new juce::AudioParameterFloat (
                juce::ParameterID { "gain", 1 }, "Gain", 0.0f, 1.0f, 1.0f));
        }

        const juce::String getName() const override { return "GainStub"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            b.applyGain (2.0f * gain->get());   // MIDI buffer left untouched = echo
        }
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return true; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock& mb) override
        {
            const float v = gain->get();
            mb.setSize (sizeof (float));
            std::memcpy (mb.getData(), &v, sizeof (float));
        }
        void setStateInformation (const void* data, int size) override
        {
            if (size == (int) sizeof (float))
            {
                float v = 0.0f;
                std::memcpy (&v, data, sizeof (float));
                *gain = v;
            }
        }
    };

    bool fail (const char* msg)
    {
        std::fprintf (stderr, "OOP smoke: %s\n", msg);
        return false;
    }

    bool expectLevel (aizana::OutOfProcessPluginInstance& proxy, float inLevel, float expected,
                      const char* what)
    {
        juce::AudioBuffer<float> buffer (2, 64);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 64; ++i)
                buffer.setSample (c, i, inLevel);

        juce::MidiBuffer midi;
        proxy.processBlock (buffer, midi);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 64; ++i)
                if (std::abs (buffer.getSample (c, i) - expected) > 1.0e-4f)
                {
                    std::fprintf (stderr, "OOP smoke: %s — sample [%d][%d] = %f, expected %f\n",
                                  what, c, i, buffer.getSample (c, i), expected);
                    return false;
                }
        return true;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // gives us a MessageManager for the IPC

    juce::String commandLine;
    for (int i = 1; i < argc; ++i)
        commandLine << (i > 1 ? " " : "") << juce::String (argv[i]);

    aizana::PluginLoader loader =
        [] (const juce::PluginDescription&, double, int, juce::String&)
            -> std::unique_ptr<juce::AudioProcessor>
        {
            return std::make_unique<GainStubProcessor>();
        };

    // ---- Worker role ----
    if (auto worker = aizana::startPluginHostWorkerIfRequested (commandLine, loader))
    {
        auto* mm = juce::MessageManager::getInstance();
        for (int guard = 0; ! worker->isFinished() && guard < 400; ++guard)   // ~20s ceiling
            mm->runDispatchLoopUntil (50);
        return 0;
    }

    // ---- Host role ----
    juce::PluginDescription desc;
    desc.name             = "SmokeGainStub";
    desc.pluginFormatName = "Test";
    desc.fileOrIdentifier = "smoke";

    auto proxy = std::make_unique<aizana::OutOfProcessPluginInstance> (desc, 2);
    bool pass = true;

    // E5.4 — parameter metadata arrived across the boundary in the constructor.
    if (proxy->getParameters().size() != 1)
        pass = fail ("expected exactly 1 hosted parameter proxy");
    else if (proxy->getParameters()[0]->getName (64) != "Gain")
        pass = fail ("parameter name did not survive the boundary");
    if (! proxy->acceptsMidi() || ! proxy->producesMidi())
        pass = fail ("MIDI flags did not survive the boundary");

    proxy->prepareToPlay (44100.0, 64);
    if (! proxy->isChildConnected())
        pass = fail ("child did not connect");

    // E5.3b — audio: 0.5 in, gain default 1.0 → 0.5 * 2 * 1.0 = 1.0.
    if (pass) pass = expectLevel (*proxy, 0.5f, 1.0f, "audio round trip");

    // E5.4 — host param proxy drives the child: gain 0.25 → 0.5 * 2 * 0.25 = 0.25.
    if (pass)
    {
        proxy->getParameters()[0]->setValue (0.25f);
        pass = expectLevel (*proxy, 0.5f, 0.25f, "param change across the boundary");
    }

    // E5.4 — MIDI both ways: note on in → echoed back out.
    if (pass)
    {
        juce::AudioBuffer<float> buffer (2, 64);
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 7);
        proxy->processBlock (buffer, midi);

        int found = 0;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getNoteNumber() == 60 && meta.samplePosition == 7)
                ++found;
        }
        if (found != 1)
            pass = fail ("MIDI note did not round-trip through the child");
    }

    // E5.4 — state: getState reflects the child's live gain (0.25), setState restores it.
    if (pass)
    {
        juce::MemoryBlock state;
        proxy->getStateInformation (state);
        float v = -1.0f;
        if (state.getSize() != sizeof (float))
            pass = fail ("getStateInformation returned wrong size");
        else
        {
            std::memcpy (&v, state.getData(), sizeof (float));
            if (std::abs (v - 0.25f) > 1.0e-4f)
                pass = fail ("state does not reflect the child's parameter value");
        }

        if (pass)
        {
            const float restored = 0.5f;
            juce::MemoryBlock newState (&restored, sizeof (float));
            proxy->setStateInformation (newState.getData(), (int) newState.getSize());
            // gain 0.5 → 0.5 * 2 * 0.5 = 0.5
            pass = expectLevel (*proxy, 0.5f, 0.5f, "setState across the boundary");
        }
    }

    proxy->releaseResources();
    proxy.reset();

    std::fprintf (stderr, pass ? "OOP smoke: PASS\n" : "OOP smoke: FAIL\n");
    return pass ? 0 : 1;
}
