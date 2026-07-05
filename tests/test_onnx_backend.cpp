#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_core/juce_core.h>

#include "ai/OnnxModelRunner.h"

#ifndef AIZANA_TEST_RESOURCE_DIR
 #define AIZANA_TEST_RESOURCE_DIR "."
#endif

namespace
{
    juce::File dummyModel()
    {
        return juce::File (AIZANA_TEST_RESOURCE_DIR).getChildFile ("dummy_scale2.onnx");
    }
}

// Without a model loaded, the backend is unavailable and run() is a defined
// passthrough — true whether or not ORT is compiled in. This is the contract the
// MixingEngine relies on to fall back to the heuristic.
TEST_CASE ("OnnxModelRunner is a passthrough until a model is loaded", "[onnx]")
{
    OnnxModelRunner runner;
    REQUIRE_FALSE (runner.isAvailable());

    const std::vector<float> in { 1.0f, -2.0f, 3.5f };
    const auto out = runner.run (in);
    REQUIRE (out == in);            // identity passthrough, no fabricated output
    REQUIRE (runner.getExpectedInputSize() == 0);
}

TEST_CASE ("OnnxModelRunner reports a missing model gracefully", "[onnx]")
{
    OnnxModelRunner runner;
    const auto missing = juce::File (AIZANA_TEST_RESOURCE_DIR).getChildFile ("does_not_exist.onnx");
    REQUIRE_FALSE (runner.loadModel (missing));
    REQUIRE_FALSE (runner.isAvailable());
}

#if AIZANA_HAVE_ONNXRUNTIME

// Real inference: the dummy model computes output = input * 2, so a correct run
// returns doubled values — proving ORT executed the graph rather than the runner
// falling back to its identity passthrough.
TEST_CASE ("OnnxModelRunner runs a real model (CoreML/CPU) and computes output", "[onnx]")
{
    REQUIRE (OnnxModelRunner::isBackendCompiledIn());
    REQUIRE (dummyModel().existsAsFile());

    OnnxModelRunner runner;
    REQUIRE (runner.loadModel (dummyModel()));
    REQUIRE (runner.isAvailable());
    REQUIRE (runner.getExpectedInputSize() == 4);          // model input is static [1,4]
    REQUIRE (runner.getName().contains ("ONNX Runtime"));   // names the active EP

    const std::vector<float> in  { 1.0f, 2.0f, 3.0f, 4.0f };
    const auto out = runner.run (in);

    REQUIRE (out.size() == in.size());
    for (size_t i = 0; i < in.size(); ++i)
        REQUIRE (out[i] == Catch::Approx (in[i] * 2.0f));
}

#else

TEST_CASE ("OnnxModelRunner stub is inert when ORT is not compiled in", "[onnx]")
{
    REQUIRE_FALSE (OnnxModelRunner::isBackendCompiledIn());
    OnnxModelRunner runner;
    REQUIRE_FALSE (runner.loadModel (dummyModel()));   // never loads in the stub build
    REQUIRE_FALSE (runner.isAvailable());
}

#endif
