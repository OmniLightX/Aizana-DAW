#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

/** One loaded drum sample (user WAV/AIFF). Owned by the processor's message-thread
    OwnedArray and never freed until the processor is destroyed, so the audio thread
    can hold a raw pointer for a voice's lifetime without any lock (I-01). */
struct DrumSampleData
{
    juce::AudioBuffer<float> buffer;         // 1 or 2 channels
    double        sourceSampleRate = 44100.0;
    juce::String  sourcePath;
    juce::String  displayName;
};

/**
 * DrumMachineProcessor — "Aizana Drums": a self-contained procedural drum
 * instrument. No sample files, no content licensing — every voice is
 * synthesized analog-style DSP, so it ships in the binary and always works.
 *
 * MIDI notes on the General-MIDI drum map trigger the voices:
 *   36 Kick · 38/40 Snare · 37 Rim · 39 Clap · 42/44 Closed Hat · 46 Open Hat
 *   41/45 Low Tom · 47/48/50 High Tom · 49 Crash · 56 Cowbell.
 *
 * Built on juce::Synthesiser so triggering is sample-accurate inside the block.
 * processBlock is allocation-/lock-free; voices synthesize per sample (I-01).
 * This is the P0 "beats out of the box" instrument (Big-4 gap analysis).
 */
class DrumMachineProcessor : public juce::AudioPluginInstance
{
public:
    DrumMachineProcessor();
    ~DrumMachineProcessor() override = default;

    // Macro params, mirrored from the APVTS each block; read by voices at note-on.
    struct Params
    {
        std::atomic<float> level     { 0.85f };  // master gain 0..1
        std::atomic<float> kickDecay { 1.0f };   // 0.3..2.0 multiplier
        std::atomic<float> snareTone { 0.5f };   // 0 = body .. 1 = noise
        std::atomic<float> hatDecay  { 1.0f };   // 0.3..2.0 multiplier
    };

    // --- built-in step sequencer (Ultrabeat-style: pads × 16 steps) -----------
    static constexpr int kNumPads  = 8;
    static constexpr int kNumSteps = 16;

    static juce::String padName (int pad);       // "Kick" .. "Tom Hi"
    static int          padNote (int pad);       // GM note the pad triggers

    bool getStep (int pad, int step) const noexcept;
    void setStep (int pad, int step, bool on) noexcept;
    void toggleStep (int pad, int step) noexcept;
    void clearPattern() noexcept;

    void setSeqEnabled (bool on) noexcept  { seqOn.store (on, std::memory_order_relaxed); }
    bool isSeqEnabled() const noexcept     { return seqOn.load (std::memory_order_relaxed); }

    int  getCurrentStep() const noexcept   { return uiStep.load (std::memory_order_relaxed); }  // -1 = not running
    void auditionPad (int pad) noexcept     { auditionReq.store (pad, std::memory_order_relaxed); }

    // --- user samples (own kick/snare/… per pad) -----------------------------
    // All message-thread; the audio thread only reads padSampleRaw() (atomic ptr).
    bool loadSample (int pad, const juce::File& file);   // reads WAV/AIFF; true on success
    void clearSample (int pad);                          // pad reverts to the synth voice
    bool padHasSample (int pad) const noexcept;
    juce::String padSampleName (int pad) const;          // filename for the UI, "" if none
    int  loadKitFolder (const juce::File& folder);       // auto-map by filename; returns loaded count

    /** MIDI note → pad index (with GM aliases), or -1 if the note is not a pad. */
    static int padForNote (int note) noexcept;

    // Called by the internal voices on the audio thread — RT-safe atomic reads only.
    Params& voiceParams() noexcept { return params; }
    DrumSampleData* padSampleRaw (int pad) const noexcept
    {
        return (pad >= 0 && pad < kNumPads) ? padSample[pad].load (std::memory_order_acquire) : nullptr;
    }

    // -------------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&,  juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}

    const juce::String getName() const override { return "Aizana Drums"; }
    bool   acceptsMidi()  const override { return true;  }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.5; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorEditor* createEditor() override;   // pad grid + step sequencer
    bool hasEditor() const override { return true; }

    void fillInPluginDescription (juce::PluginDescription&) const override;

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::Synthesiser synth;
    Params params;

    std::atomic<float>* pLevel     = nullptr;
    std::atomic<float>* pKickDecay = nullptr;
    std::atomic<float>* pSnareTone = nullptr;
    std::atomic<float>* pHatDecay  = nullptr;

    double sampleRate = 44100.0;   // stored in prepareToPlay — getSampleRate() is 0 until the host sets it

    // Sequencer state — 16-bit step mask per pad; transport-synced in processBlock.
    std::atomic<juce::uint16> pattern[kNumPads] {};
    std::atomic<bool> seqOn      { false };
    std::atomic<int>  uiStep     { -1 };
    std::atomic<int>  auditionReq { -1 };

    // User samples: audio thread reads padSample[] (atomic raw ptr); the objects
    // live in loadedSamples (message thread), freed only in the destructor → the
    // raw pointer a voice grabbed at note-on stays valid for its whole lifetime.
    std::atomic<DrumSampleData*> padSample[kNumPads] {};
    juce::OwnedArray<DrumSampleData> loadedSamples;
    juce::String padSamplePath[kNumPads];
    juce::AudioFormatManager formatManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumMachineProcessor)
};
