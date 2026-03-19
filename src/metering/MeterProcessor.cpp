#include "MeterProcessor.h"
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Helper: convert linear amplitude to dBFS, clamped to -100 dBFS minimum
// ---------------------------------------------------------------------------
static float toDBFS(float linearAmplitude)
{
    if (linearAmplitude <= 0.0f)
        return -100.0f;
    return std::max(-100.0f, 20.0f * std::log10(linearAmplitude));
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

MeterProcessor::MeterProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input",   juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

MeterProcessor::~MeterProcessor()
{
    releaseResources();
}

// ---------------------------------------------------------------------------
// prepareToPlay — message thread
// ---------------------------------------------------------------------------

void MeterProcessor::prepareToPlay(double sampleRate, int /*maximumExpectedSamplesPerBlock*/)
{
    juce::ScopedLock sl(ebuLock);

    currentSampleRate = sampleRate;
    numChannels = 2;

    // Resize the backing buffer for the FIFO (kFifoSize frames × numChannels)
    meterBuffer.setSize(numChannels, kFifoSize, false, true, false);
    meterFifo.reset();

    // Destroy any existing state and create fresh libebur128 state.
    // Mode: I (integrated) + M (momentary) + S (short-term).
    if (ebuState != nullptr)
    {
        ebur128_destroy(&ebuState);
        ebuState = nullptr;
    }

    ebuState = ebur128_init(static_cast<unsigned>(numChannels),
                             static_cast<unsigned long>(sampleRate),
                             EBUR128_MODE_I | EBUR128_MODE_M | EBUR128_MODE_S);

    // Reset atomic meters
    peakDB.store(-100.0f);
    rmsDB.store(-100.0f);
    lufsM.store(-100.0f);
    lufsI.store(-100.0f);
}

// ---------------------------------------------------------------------------
// releaseResources — message thread
// ---------------------------------------------------------------------------

void MeterProcessor::releaseResources()
{
    juce::ScopedLock sl(ebuLock);

    if (ebuState != nullptr)
    {
        ebur128_destroy(&ebuState);
        ebuState = nullptr;
    }
}

// ---------------------------------------------------------------------------
// processBlock — AUDIO THREAD
//
// CRITICAL: No libebur128 calls here. Only lock-free FIFO write + peak atomic.
// This follows RESEARCH.md Pitfall 6 (spectrum/meter on audio thread causes
// data races and allocation issues).
// ---------------------------------------------------------------------------

void MeterProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    const int numSamples = buffer.getNumSamples();
    const int channels   = std::min(buffer.getNumChannels(), numChannels);

    // --- Instantaneous peak (atomic store — safe on audio thread) ---
    float maxMagnitude = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
        maxMagnitude = std::max(maxMagnitude, buffer.getMagnitude(ch, 0, numSamples));

    // Use relaxed ordering — the message thread reads this with relaxed ordering too.
    // A slight inconsistency (one-frame stale peak) is acceptable for a peak meter.
    const float newPeak = toDBFS(maxMagnitude);
    float current = peakDB.load(std::memory_order_relaxed);
    if (newPeak > current)
        peakDB.store(newPeak, std::memory_order_relaxed);

    // --- Lock-free FIFO write (from RESEARCH.md §LUFS Metering) ---
    // juce::AbstractFifo::prepareToWrite returns two regions (for wrap-around).
    // We only copy from the first channel into channel 0 of the meter buffer,
    // and the second channel into channel 1 (if stereo).
    int start1, size1, start2, size2;
    meterFifo.prepareToWrite(numSamples, start1, size1, start2, size2);

    for (int ch = 0; ch < channels && ch < meterBuffer.getNumChannels(); ++ch)
    {
        if (size1 > 0)
            meterBuffer.copyFrom(ch, start1, buffer, ch, 0, size1);
        if (size2 > 0)
            meterBuffer.copyFrom(ch, start2, buffer, ch, size1, size2);
    }

    meterFifo.finishedWrite(size1 + size2);

    // Audio passthrough — MeterProcessor is transparent (insert on any track)
    // No-op: buffer is already the audio output. No processing applied.
}

// ---------------------------------------------------------------------------
// flushFifo — MESSAGE THREAD ONLY
//
// Reads all pending samples from the FIFO, feeds libebur128, and updates
// the atomic meter values. Called by timerCallback (in production) or directly
// in tests (headless — no timer).
//
// libebur128 expects interleaved samples: [L0, R0, L1, R1, ...].
// We have planar (per-channel) storage in meterBuffer, so we interleave
// into a temporary stack buffer before calling libebur128_add_frames_float().
// ---------------------------------------------------------------------------

void MeterProcessor::flushFifo()
{
    juce::ScopedLock sl(ebuLock);

    if (ebuState == nullptr)
        return;

    const int available = meterFifo.getNumReady();
    if (available <= 0)
        return;

    // Read up to 'available' samples in up to two contiguous regions.
    int start1, size1, start2, size2;
    meterFifo.prepareToRead(available, start1, size1, start2, size2);

    // Interleaved buffer for libebur128 (stack allocation — message thread, no RT constraint)
    std::vector<float> interleaved;
    interleaved.reserve(static_cast<size_t>(available * numChannels));

    // Region 1
    for (int i = 0; i < size1; ++i)
    {
        for (int ch = 0; ch < numChannels && ch < meterBuffer.getNumChannels(); ++ch)
            interleaved.push_back(meterBuffer.getSample(ch, start1 + i));
        // If source has fewer channels, pad with the last channel (mono → stereo duplication)
        for (int ch = meterBuffer.getNumChannels(); ch < numChannels; ++ch)
            interleaved.push_back(meterBuffer.getSample(meterBuffer.getNumChannels() - 1, start1 + i));
    }

    // Region 2 (wraps around end of ring buffer)
    for (int i = 0; i < size2; ++i)
    {
        for (int ch = 0; ch < numChannels && ch < meterBuffer.getNumChannels(); ++ch)
            interleaved.push_back(meterBuffer.getSample(ch, start2 + i));
        for (int ch = meterBuffer.getNumChannels(); ch < numChannels; ++ch)
            interleaved.push_back(meterBuffer.getSample(meterBuffer.getNumChannels() - 1, start2 + i));
    }

    meterFifo.finishedRead(size1 + size2);

    // Feed interleaved frames to libebur128
    const size_t numFrames = static_cast<size_t>(size1 + size2);
    if (numFrames > 0)
        ebur128_add_frames_float(ebuState, interleaved.data(), numFrames);

    // --- RMS: computed from the raw samples we just read ---
    // RMS = sqrt(mean of squares) across all frames and channels
    double sumSq = 0.0;
    for (float s : interleaved)
        sumSq += static_cast<double>(s) * static_cast<double>(s);

    const double meanSq = (numFrames > 0)
                          ? sumSq / static_cast<double>(numFrames * static_cast<size_t>(numChannels))
                          : 0.0;

    rmsDB.store(toDBFS(static_cast<float>(std::sqrt(meanSq))), std::memory_order_relaxed);

    // --- LUFS momentary (400 ms window) ---
    double momentary = -100.0;
    if (ebur128_loudness_momentary(ebuState, &momentary) == EBUR128_SUCCESS)
    {
        lufsM.store(std::isfinite(momentary)
                    ? static_cast<float>(momentary)
                    : -100.0f,
                    std::memory_order_relaxed);
    }

    // --- LUFS integrated (full program, gated) ---
    double integrated = -100.0;
    if (ebur128_loudness_global(ebuState, &integrated) == EBUR128_SUCCESS)
    {
        lufsI.store(std::isfinite(integrated)
                    ? static_cast<float>(integrated)
                    : -100.0f,
                    std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------------------
// pushAudioData — AUDIO THREAD
//
// Called from the AudioDeviceManager IO callback after Tracktion Engine has
// filled the output buffer. Feeds the master MeterProcessor without requiring
// insertion into the TE plugin chain.
//
// CRITICAL: Same audio-thread invariants as processBlock apply here:
//   - No libebur128 calls — only FIFO write and atomic peak store.
//   - No heap allocations.
//   - No locks.
// ---------------------------------------------------------------------------

void MeterProcessor::pushAudioData (const float* const* channelData,
                                     int numChans,
                                     int numSamples)
{
    if (channelData == nullptr || numSamples <= 0 || numChans <= 0)
        return;

    const int channels = std::min (numChans, numChannels);

    // --- Instantaneous peak (atomic store — safe on audio thread) ---
    float maxMagnitude = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
    {
        if (channelData[ch] == nullptr)
            continue;
        for (int i = 0; i < numSamples; ++i)
            maxMagnitude = std::max (maxMagnitude, std::abs (channelData[ch][i]));
    }

    const float newPeak = toDBFS (maxMagnitude);
    float current = peakDB.load (std::memory_order_relaxed);
    if (newPeak > current)
        peakDB.store (newPeak, std::memory_order_relaxed);

    // --- Lock-free FIFO write ---
    // Write channel 0 (and channel 1 if stereo) into the meter buffer.
    int start1, size1, start2, size2;
    meterFifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    for (int ch = 0; ch < channels && ch < meterBuffer.getNumChannels(); ++ch)
    {
        if (channelData[ch] == nullptr)
            continue;
        if (size1 > 0)
            meterBuffer.copyFromWithRamp (ch, start1, channelData[ch],        size1, 1.0f, 1.0f);
        if (size2 > 0)
            meterBuffer.copyFromWithRamp (ch, start2, channelData[ch] + size1, size2, 1.0f, 1.0f);
    }

    meterFifo.finishedWrite (size1 + size2);
}

// ---------------------------------------------------------------------------
// Thread-safe getters
// ---------------------------------------------------------------------------

float MeterProcessor::getPeakDB() const
{
    return peakDB.load(std::memory_order_relaxed);
}

float MeterProcessor::readAndResetPeakDB()
{
    // Atomically read the current peak and reset to -100 dBFS.
    // Each timerCallback tick sees only the peak from the current window —
    // prevents the ratchet-upward-forever behaviour where a past audio burst
    // keeps peakDB stuck at its max level until the next burst exceeds it.
    return peakDB.exchange (-100.0f, std::memory_order_relaxed);
}

float MeterProcessor::getRMSDB() const
{
    return rmsDB.load(std::memory_order_relaxed);
}

float MeterProcessor::getLUFSMomentary() const
{
    return lufsM.load(std::memory_order_relaxed);
}

float MeterProcessor::getLUFSIntegrated() const
{
    return lufsI.load(std::memory_order_relaxed);
}
