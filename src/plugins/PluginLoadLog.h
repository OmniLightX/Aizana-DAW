#pragma once

#include <juce_core/juce_core.h>

/**
 * CloudesPluginLog — flush-immediately stage logger for diagnosing plugin-load
 * hangs (review 2026-06-09: some commercial plugins, e.g. SSL/Eiosis, freeze the
 * app on instantiation and require a hard quit).
 *
 * Each call appends ONE timestamped line to
 *   ~/Library/Logs/Cloudes/plugin-load.log
 * and closes the file immediately, so the trail survives a hard quit — the LAST
 * line written is the stage that hung. Works in Release builds (unlike DBG).
 *
 * Cheap and side-effect-free; safe to leave in. Only ever called on the message
 * thread (plugin instantiation + editor creation), never the audio thread.
 */
namespace CloudesPluginLog
{
    inline void stage (const juce::String& message)
    {
        auto file = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                        .getChildFile ("Library/Logs/Cloudes/plugin-load.log");
        file.getParentDirectory().createDirectory();
        file.appendText (juce::Time::getCurrentTime().toString (true, true, true, true)
                         + "  " + message + juce::newLine);
    }
}
