#include "OnnxModelRunner.h"

std::vector<float> OnnxModelRunner::run (const std::vector<float>& input)
{
    // No model loaded in Phase 2 — defined passthrough (identity). We do not fake
    // inference output. Phase 3 runs the real model via ONNX Runtime + CoreML here.
    if (! modelLoaded)
        return input;

    return {};
}
