#pragma once

#include "engine/AICommandBridge.h"
#include "ai/IInferenceBackend.h"
#include <memory>
#include <functional>

/**
 * AIEngine — the background worker pool (Phase 2, Block B scaffolding).
 *
 * Architecture invariant (CLAUDE.md): "AI inference always on a dedicated background
 * thread — NEVER on or near the audio thread" and "AI->TE mutations ONLY via the
 * Command Queue". AIEngine enforces both: work runs on a juce::ThreadPool, and any
 * resulting Edit mutation goes back through the AICommandBridge (which applies it on
 * the message thread as ONE undo transaction). The Edit is never touched directly here.
 *
 * Phase 2 = plumbing. It owns a swappable IInferenceBackend (OnnxModelRunner stub by
 * default); Phase 3's real worker (stem separation, instrument detection, mixing model)
 * slots in unchanged.
 */
class AIEngine
{
public:
    explicit AIEngine (AICommandBridge& bridge);
    ~AIEngine();

    /** Produces a command batch (typically from inference results). Runs on a worker. */
    using BatchProducer = std::function<std::vector<AICommandBridge::Command>()>;

    /** Run `produce` on a background thread; enqueue its batch on the bridge (applied on
     *  the message thread as one undo step). Empty batches are ignored. */
    void submitBatch (const juce::String& description, BatchProducer produce);

    /** Generic background task with an optional message-thread completion callback. */
    void submit (std::function<void()> backgroundWork, std::function<void()> onComplete = {});

    IInferenceBackend* getBackend() const noexcept { return backend.get(); }
    void setBackend (std::unique_ptr<IInferenceBackend> newBackend);

    /** Loads an ONNX model into the backend if it is an OnnxModelRunner. Synchronous —
        call from a worker (it builds an ORT session). Returns true on success; a missing
        or invalid model leaves the backend unavailable (callers fall back to heuristics).
        Slice 5a: dropping a model at the path below activates local ML with no rebuild. */
    bool tryLoadModel (const juce::File& modelFile);

    int  getNumThreads() const noexcept;

    /** Block until all queued jobs finish (for tests / clean shutdown). */
    void waitUntilIdle (int timeoutMs);

private:
    AICommandBridge&                   bridge;
    std::unique_ptr<IInferenceBackend> backend;
    juce::ThreadPool                   pool;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIEngine)
};
