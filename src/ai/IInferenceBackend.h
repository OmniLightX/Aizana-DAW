#pragma once

#include <juce_core/juce_core.h>
#include <vector>

/**
 * IInferenceBackend — replaceable interface for model inference.
 *
 * Abstracts "run a model" so the rest of Aizana never depends on a concrete ML
 * runtime. OnnxModelRunner provides the ONNX Runtime implementation when enabled
 * at build time, with optional CoreML acceleration and CPU fallback. Inference
 * runs on a background thread (AIEngine), never on the audio thread.
 */
class IInferenceBackend
{
public:
    virtual ~IInferenceBackend() = default;

    /** Human-readable backend name (for diagnostics / preferences). */
    virtual juce::String getName() const = 0;

    /** True once a real model is loaded and inference is meaningful. */
    virtual bool isAvailable() const = 0;

    /** Run inference on a flat input feature vector, returning the output vector.
     *  Until a model is available this is a defined no-op (see the concrete backend). */
    virtual std::vector<float> run (const std::vector<float>& input) = 0;
};
