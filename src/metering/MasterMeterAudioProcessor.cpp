#include "MasterMeterAudioProcessor.h"
#include "MeterProcessor.h"

//==============================================================================
MasterMeterAudioProcessor::MasterMeterAudioProcessor (MeterProcessor& meter)
    : masterMeterProcessor (meter)
{
}

//==============================================================================
void MasterMeterAudioProcessor::prepareToPlay (double sampleRate,
                                               int maximumExpectedSamplesPerBlock)
{
    // Delegate to MeterProcessor so its FIFO and libebur128 state are configured
    // for the device parameters TE will use when calling processBlock().
    masterMeterProcessor.prepareToPlay (sampleRate, maximumExpectedSamplesPerBlock);
}

void MasterMeterAudioProcessor::releaseResources()
{
    masterMeterProcessor.releaseResources();
}

//==============================================================================
void MasterMeterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& /*midiMessages*/)
{
    // Audio thread — no allocations, no locks, no I/O.
    //
    // Tap the buffer for metering. pushAudioData() uses atomics and a lock-free FIFO;
    // it is safe to call on the audio thread.
    //
    // The buffer is NOT modified — this is a read-only probe. TE continues to route
    // the same buffer to the hardware output unchanged.
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if (numChannels <= 0 || numSamples <= 0)
        return;

    // Build a channel-pointer array from the AudioBuffer and call pushAudioData.
    // AudioBuffer::getArrayOfReadPointers() returns const float* const* — cast to
    // the signature expected by pushAudioData (which reads but does not modify).
    masterMeterProcessor.pushAudioData (buffer.getArrayOfReadPointers(),
                                        numChannels,
                                        numSamples);
}
