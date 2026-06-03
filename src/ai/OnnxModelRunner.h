#pragma once

#include "ai/IInferenceBackend.h"

/**
 * OnnxModelRunner — Phase 2 SCAFFOLDING (no real model, no ONNX Runtime dependency yet).
 *
 * This is the seam where ONNX Runtime + the CoreML execution provider (Apple Neural
 * Engine) plug in for Phase 3. Until a model is loaded, isAvailable() is false and
 * run() is a defined passthrough (identity) — we deliberately do NOT fabricate AI
 * output. Keeping this a stub avoids pulling a heavy runtime into the Phase 2 build
 * while locking the interface so Phase 3 is a drop-in.
 */
class OnnxModelRunner : public IInferenceBackend
{
public:
    OnnxModelRunner() = default;

    juce::String getName() const override   { return "ONNX Runtime (CoreML) - scaffold"; }
    bool isAvailable() const override        { return modelLoaded; }
    std::vector<float> run (const std::vector<float>& input) override;

    // Phase 3: bool loadModel (const juce::File&);  // creates session + CoreML provider

private:
    bool modelLoaded = false;   // no real model in Phase 2
};
