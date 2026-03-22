#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>

class BuiltInPluginFormat;

enum class ReverbMode { Room, Hall, Plate };

/**
 * ReverbProcessor — Multi-mode algorithmic reverb (Room / Hall / Plate).
 *
 * Algorithm: Schroeder/Moorer hybrid — parallel comb filters feeding
 * a series of allpass diffusers. Mode-specific parameters:
 *
 *   Room:  4 comb + 2 allpass, short delays (25-80ms), higher damping
 *   Hall:  8 comb + 4 allpass, long delays (50-150ms), wide stereo spread
 *   Plate: 4 comb + 4 allpass, modulated delays (chorus-like diffusion)
 *
 * Audio-thread discipline (RESEARCH.md Pattern 2):
 *   - All delay line memory is pre-allocated in prepareToPlay().
 *   - processBlock() has zero dynamic allocations.
 *   - Mode changes update delay params atomically (no reallocation needed
 *     because delay lines are pre-sized for the worst-case sample rate).
 *
 * FX-03 requirements: Room/Hall/Plate modes, wet/dry control, no latency.
 */
class ReverbProcessor : public juce::AudioPluginInstance
{
public:
    ReverbProcessor();
    ~ReverbProcessor() override = default;

    void setMode(ReverbMode mode);
    void setRoomSize(float size);     // 0.0 to 1.0
    void setDamping(float damping);   // 0.0 to 1.0
    void setPreDelay(float ms);       // 0 to 100ms
    void setWetDry(float wet);        // 0.0 to 1.0 (wet level)

    // -------------------------------------------------------------------------
    // juce::AudioProcessor interface
    // -------------------------------------------------------------------------
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    void releaseResources() override;

    const juce::String getName() const override { return "ReverbProcessor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // AudioPluginInstance interface
    void fillInPluginDescription (juce::PluginDescription& desc) const override;

private:
    // -------------------------------------------------------------------------
    // Internal comb filter: delay line + damping low-pass in the feedback path
    // -------------------------------------------------------------------------
    struct CombFilter
    {
        std::vector<float> buffer;
        int writePos   = 0;
        int delaySamples = 0;
        float feedback = 0.5f;
        float damp     = 0.5f;
        float filterState = 0.0f;

        void prepare(int maxDelaySamples)
        {
            buffer.assign(static_cast<size_t>(maxDelaySamples), 0.0f);
            writePos = 0;
        }

        void setDelay(int samples)
        {
            delaySamples = juce::jmin(samples, (int)buffer.size() - 1);
        }

        float process(float input) noexcept
        {
            int readPos = writePos - delaySamples;
            if (readPos < 0) readPos += (int)buffer.size();

            float delayed = buffer[static_cast<size_t>(readPos)];

            // Lowpass damping in feedback path
            filterState = delayed * (1.0f - damp) + filterState * damp;

            buffer[static_cast<size_t>(writePos)] = input + filterState * feedback;
            writePos = (writePos + 1) % (int)buffer.size();

            return delayed;
        }

        void reset()
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
            filterState = 0.0f;
        }
    };

    // -------------------------------------------------------------------------
    // Allpass diffuser
    // -------------------------------------------------------------------------
    struct AllpassFilter
    {
        std::vector<float> buffer;
        int writePos   = 0;
        int delaySamples = 0;
        float gain     = 0.5f;

        void prepare(int maxDelaySamples)
        {
            buffer.assign(static_cast<size_t>(maxDelaySamples), 0.0f);
            writePos = 0;
        }

        void setDelay(int samples)
        {
            delaySamples = juce::jmin(samples, (int)buffer.size() - 1);
        }

        float process(float input) noexcept
        {
            int readPos = writePos - delaySamples;
            if (readPos < 0) readPos += (int)buffer.size();

            float delayed = buffer[static_cast<size_t>(readPos)];
            float out = -gain * input + delayed;
            buffer[static_cast<size_t>(writePos)] = input + gain * delayed;
            writePos = (writePos + 1) % (int)buffer.size();
            return out;
        }

        void reset()
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
        }
    };

    // -------------------------------------------------------------------------
    // Room/Hall have 8 combs; Plate uses 4 — allocate for worst case.
    // -------------------------------------------------------------------------
    static constexpr int MaxCombs    = 8;
    static constexpr int MaxAllpasses = 4;

    std::array<CombFilter,    MaxCombs>    combsL, combsR;
    std::array<AllpassFilter, MaxAllpasses> allpassL, allpassR;

    // Mode parameters
    std::atomic<int>   currentMode { 0 };  // ReverbMode cast to int
    std::atomic<float> roomSizeParam { 0.5f };
    std::atomic<float> dampingParam  { 0.5f };
    std::atomic<float> wetParam      { 0.3f };

    double   currentSampleRate = 44100.0;
    int      numActiveCombs    = 4;
    int      numActiveAllpasses = 2;

    // Pre-delay line (max 100ms at any sample rate)
    std::vector<float> preDelayBuffer;
    int preDelayWrite = 0;
    int preDelaySamples = 0;

    std::atomic<float> preDelayMs { 0.0f };

    // Configure delay lengths and feedback for the current mode
    void applyModeParameters();

    // Max delay samples for pre-allocation
    static int maxDelaySamplesForRate(double sr)
    {
        // Hall mode can go up to 150ms — allocate for 200ms headroom
        return static_cast<int>(sr * 0.200);
    }
};
