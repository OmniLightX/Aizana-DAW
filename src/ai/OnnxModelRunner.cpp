#include "OnnxModelRunner.h"

#if CLOUDES_HAVE_ONNXRUNTIME
 #include <onnxruntime_cxx_api.h>
 #include <coreml_provider_factory.h>
#endif

//==============================================================================
#if CLOUDES_HAVE_ONNXRUNTIME

struct OnnxModelRunner::Impl
{
    Ort::Env env { ORT_LOGGING_LEVEL_WARNING, "cloudes" };
    std::unique_ptr<Ort::Session> session;

    std::string          inputName, outputName;
    std::vector<int64_t> inputShape;      // model's declared shape (may hold -1 = dynamic)
    int64_t              staticInputElems = 0;   // product of dims if fully static, else 0
};

OnnxModelRunner::OnnxModelRunner()
    : impl (std::make_unique<Impl>()),
      backendName ("ONNX Runtime (no model loaded)")
{
}

OnnxModelRunner::~OnnxModelRunner() = default;

bool OnnxModelRunner::loadModel (const juce::File& modelFile)
{
    modelLoaded = false;

    if (! modelFile.existsAsFile())
    {
        backendName = "ONNX Runtime (model not found)";
        return false;
    }

    try
    {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads (1);
        opts.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Prefer CoreML (routes to the Neural Engine when the graph allows). On Intel
        // or when CoreML rejects the model this throws — we then run on CPU.
        bool coreml = false;
        try
        {
            Ort::ThrowOnError (
                OrtSessionOptionsAppendExecutionProvider_CoreML (opts, COREML_FLAG_USE_NONE));
            coreml = true;
        }
        catch (const Ort::Exception&)
        {
            coreml = false;   // CPU EP is always present as the default fallback
        }

        const auto path = modelFile.getFullPathName().toStdString();
        impl->session = std::make_unique<Ort::Session> (impl->env, path.c_str(), opts);

        Ort::AllocatorWithDefaultOptions alloc;
        impl->inputName  = impl->session->GetInputNameAllocated  (0, alloc).get();
        impl->outputName = impl->session->GetOutputNameAllocated (0, alloc).get();

        impl->inputShape = impl->session->GetInputTypeInfo (0)
                               .GetTensorTypeAndShapeInfo().GetShape();

        impl->staticInputElems = 1;
        for (auto d : impl->inputShape)
        {
            if (d > 0) { impl->staticInputElems *= d; }
            else       { impl->staticInputElems = 0; break; }   // dynamic axis present
        }

        backendName = juce::String ("ONNX Runtime / ") + (coreml ? "CoreML (Neural Engine)" : "CPU");
        modelLoaded = true;
        return true;
    }
    catch (const std::exception& e)
    {
        DBG ("OnnxModelRunner::loadModel failed: " << e.what());
        impl->session.reset();
        backendName = "ONNX Runtime (load failed)";
        return false;
    }
}

std::vector<float> OnnxModelRunner::run (const std::vector<float>& input)
{
    if (! modelLoaded || impl->session == nullptr)
        return input;   // defined passthrough until a model is loaded

    try
    {
        // Resolve the run-time shape: keep the model's static dims, and size any
        // single dynamic axis so the element product equals input.size().
        std::vector<int64_t> shape = impl->inputShape;
        if (shape.empty())
        {
            shape = { (int64_t) input.size() };
        }
        else
        {
            int64_t known = 1; int dynAxis = -1;
            for (size_t i = 0; i < shape.size(); ++i)
                if (shape[i] > 0) known *= shape[i];
                else              dynAxis = (int) i;

            if (dynAxis >= 0 && known > 0)
                shape[(size_t) dynAxis] = (int64_t) (input.size() / (size_t) known);
        }

        auto memInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);

        std::vector<float> in = input;   // ORT wants a non-const data pointer
        Ort::Value inputTensor = Ort::Value::CreateTensor<float> (
            memInfo, in.data(), in.size(), shape.data(), shape.size());

        const char* inNames[]  = { impl->inputName.c_str() };
        const char* outNames[] = { impl->outputName.c_str() };

        auto outputs = impl->session->Run (Ort::RunOptions { nullptr },
                                           inNames, &inputTensor, 1, outNames, 1);

        if (outputs.empty() || ! outputs.front().IsTensor())
            return input;

        const float* data = outputs.front().GetTensorData<float>();
        const auto   count = outputs.front().GetTensorTypeAndShapeInfo().GetElementCount();
        return std::vector<float> (data, data + count);
    }
    catch (const std::exception& e)
    {
        DBG ("OnnxModelRunner::run failed: " << e.what());
        return input;   // never throw across the inference seam — degrade to passthrough
    }
}

int OnnxModelRunner::getExpectedInputSize() const noexcept
{
    return modelLoaded ? (int) impl->staticInputElems : 0;
}

bool OnnxModelRunner::isBackendCompiledIn() noexcept { return true; }

//==============================================================================
#else // !CLOUDES_HAVE_ONNXRUNTIME — no-op stub (build configured -DCLOUDES_ENABLE_ONNX=OFF)

struct OnnxModelRunner::Impl {};

OnnxModelRunner::OnnxModelRunner()
    : impl (std::make_unique<Impl>()),
      backendName ("ONNX Runtime (not compiled in)")
{
}

OnnxModelRunner::~OnnxModelRunner() = default;

bool OnnxModelRunner::loadModel (const juce::File& modelFile)
{
    juce::ignoreUnused (modelFile);
    return false;
}

std::vector<float> OnnxModelRunner::run (const std::vector<float>& input)
{
    return input;   // defined passthrough — identical observable behaviour to the loaded-but-no-model case
}

int  OnnxModelRunner::getExpectedInputSize() const noexcept { return 0; }
bool OnnxModelRunner::isBackendCompiledIn() noexcept        { return false; }

#endif
