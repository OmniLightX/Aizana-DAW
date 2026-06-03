#include "AIEngine.h"
#include "ai/OnnxModelRunner.h"

AIEngine::AIEngine (AICommandBridge& bridge_)
    : bridge (bridge_),
      backend (std::make_unique<OnnxModelRunner>()),
      pool (2)   // small dedicated AI worker pool — never the audio threads
{
}

AIEngine::~AIEngine()
{
    pool.removeAllJobs (true, 2000);   // interrupt + wait up to 2s on shutdown
}

void AIEngine::submitBatch (const juce::String& description, BatchProducer produce)
{
    pool.addJob ([this, description, produce = std::move (produce)]
    {
        auto commands = produce ? produce() : std::vector<AICommandBridge::Command>{};
        if (! commands.empty())
            bridge.enqueueBatch (description, std::move (commands));  // thread-safe; message thread applies
    });
}

void AIEngine::submit (std::function<void()> backgroundWork, std::function<void()> onComplete)
{
    pool.addJob ([work = std::move (backgroundWork), done = std::move (onComplete)]
    {
        if (work) work();
        if (done) juce::MessageManager::callAsync (done);
    });
}

void AIEngine::setBackend (std::unique_ptr<IInferenceBackend> newBackend)
{
    backend = std::move (newBackend);
}

int AIEngine::getNumThreads() const noexcept
{
    return pool.getNumThreads();
}

void AIEngine::waitUntilIdle (int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (pool.getNumJobs() > 0 && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep (5);
}
