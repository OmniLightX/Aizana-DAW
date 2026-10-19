#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include "ScopedTestLogger.h"
#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace
{
class DiagnosticLogger final : public juce::Logger
{
public:
    std::atomic<unsigned> failures { 0 };
    void logMessage (const juce::String& message) override
    {
        std::fprintf (stderr, "%s\n", message.toRawUTF8());
        if (message.startsWith ("JUCE Assertion failure")
            || message.startsWith ("*** Leaked objects detected:"))
        {
            ++failures;
            if (std::getenv ("AIZANA_TRACE_ASSERTIONS") != nullptr)
                std::fprintf (stderr, "%s\n", juce::SystemStats::getStackBacktrace().toRawUTF8());
        }
    }
};
DiagnosticLogger* diagnosticLogger = nullptr;

class CaseTrace final : public Catch::EventListenerBase
{
public:
    using EventListenerBase::EventListenerBase;
    void testCaseStarting (const Catch::TestCaseInfo& info) override
    {
        std::fprintf (stderr, "TEST CASE: %s\n", info.name.c_str());
    }
};
CATCH_REGISTER_LISTENER (CaseTrace)
}

void forwardTestLog (const juce::String& message)
{
    if (diagnosticLogger != nullptr)
        diagnosticLogger->logMessage (message);
}

int main (int argc, char** argv)
{
    DiagnosticLogger logger;
    diagnosticLogger = &logger;
    juce::Logger::setCurrentLogger (&logger);
    int result = 0;
    {
        // Engine, timers and plugin editors must share one JUCE lifetime.
        // Keep the logger alive through JUCE shutdown as well.
        juce::ScopedJuceInitialiser_GUI juceLifetime;
        result = Catch::Session().run (argc, argv);
    }
    juce::Logger::setCurrentLogger (nullptr);
    diagnosticLogger = nullptr;
    if (logger.failures.load() != 0)
        std::fprintf (stderr, "JUCE diagnostic gate: %u failures\n", logger.failures.load());
    return result != 0 ? result : (logger.failures.load() == 0 ? 0 : 1);
}

// Explicit subprocess probe; excluded from the ordinary suite. Exit must be 1
// even when a test temporarily installs a logger and Catch assertions pass.
TEST_CASE ("JUCE diagnostic gate self-test", "[.diagnostic-gate]")
{
    ScopedTestLogger capture;
    jassertfalse;
    CHECK (true);
}

namespace
{
struct IntentionalLeakProbe
{
    IntentionalLeakProbe() = default;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntentionalLeakProbe)
};
}
TEST_CASE ("Process-end leak gate self-test", "[.diagnostic-leak]")
{
    auto* intentionalLeak = new IntentionalLeakProbe();
    juce::ignoreUnused (intentionalLeak);
    CHECK (true);
}
