#pragma once

#include <juce_core/juce_core.h>
#include <vector>

/**
 * IInferenceBackend — the swappable inference seam (Phase 2, Block B scaffolding).
 *
 * Abstracts "run a model" so the rest of Aizana never depends on a concrete ML
 * runtime. Phase 2 ships a stub (OnnxModelRunner) with no real model; Phase 3 backs
 * it with ONNX Runtime + the CoreML execution provider (Apple Neural Engine) WITHOUT
 * touching callers. Inference always runs on a background thread (AIEngine) — never
 * on or near the audio thread.
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
