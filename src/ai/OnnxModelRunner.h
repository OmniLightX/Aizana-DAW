#pragma once

#include "ai/IInferenceBackend.h"
#include <juce_core/juce_core.h>
#include <memory>

/**
 * OnnxModelRunner — the real local inference backend (Phase 4 / Slice 5a).
 *
 * Runs an ONNX model through ONNX Runtime with the CoreML execution provider so
 * inference lands on the Apple Neural Engine when available (CPU fallback on
 * Intel / when CoreML can't take the graph). This is the concrete IInferenceBackend
 * the AIEngine owns; the rest of Cloudes never sees an ORT header — all of it is
 * hidden behind a PIMPL.
 *
 * Until a model is loaded, isAvailable() is false and run() is a defined passthrough
 * (identity) — we never fabricate inference output. If the build is configured with
 * -DCLOUDES_ENABLE_ONNX=OFF (no CLOUDES_HAVE_ONNXRUNTIME), this compiles as that same
 * no-op stub, so callers behave identically whether or not ORT is linked.
 *
 * Threading: construct/load/run on a background thread only (AIEngine pool) — never
 * on or near the audio thread.
 */
class OnnxModelRunner : public IInferenceBackend
{
public:
    OnnxModelRunner();
    ~OnnxModelRunner() override;

    juce::String getName() const override     { return backendName; }
    bool isAvailable() const override         { return modelLoaded; }
    std::vector<float> run (const std::vector<float>& input) override;

    /** Loads an ONNX model and creates a session, preferring the CoreML EP (Neural
        Engine) and falling back to CPU. Returns true on success; false leaves the
        runner unavailable (callers fall back to the heuristic). Safe to call again. */
    bool loadModel (const juce::File& modelFile);

    /** Length the loaded model expects, or 0 if dynamic / no model. Lets callers
        size their feature vector to the model's static input. */
    int getExpectedInputSize() const noexcept;

    /** True when this build was compiled with ONNX Runtime (vs the no-op stub). */
    static bool isBackendCompiledIn() noexcept;

private:
    struct Impl;                    // hides all ONNX Runtime types from callers
    std::unique_ptr<Impl> impl;
    bool         modelLoaded = false;
    juce::String backendName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OnnxModelRunner)
};
