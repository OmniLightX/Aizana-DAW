#pragma once
#include <juce_core/juce_core.h>

void forwardTestLog (const juce::String&);

// Capture locally without hiding assertions from the process-wide diagnostic gate.
class ScopedTestLogger final : public juce::Logger
{
public:
    ScopedTestLogger() : previous (juce::Logger::getCurrentLogger())
    {
        juce::Logger::setCurrentLogger (this);
    }
    ~ScopedTestLogger() override { juce::Logger::setCurrentLogger (previous); }
    juce::StringArray lines;

private:
    juce::Logger* previous;
    void logMessage (const juce::String& message) override
    {
        lines.add (message);
        forwardTestLog (message);
    }
};
