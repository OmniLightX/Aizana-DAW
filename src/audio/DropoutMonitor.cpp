#include "DropoutMonitor.h"

DropoutMonitor::DropoutMonitor (juce::AudioDeviceManager& adm,
                                std::function<juce::String()> contextProvider)
    : deviceManager (adm), getContext (std::move (contextProvider))
{
    logLine ("=== DropoutMonitor gestartet " + juce::Time::getCurrentTime().toISO8601 (true) + " ===");
    startTimer (1000);
}

DropoutMonitor::~DropoutMonitor()
{
    stopTimer();
}

juce::File DropoutMonitor::getLogFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Aizana").getChildFile ("logs").getChildFile ("dropouts.log");
}

void DropoutMonitor::timerCallback()
{
    // getXRunCount() zählt Under-/Overruns seit Device-Start; -1 = Backend kann es nicht.
    const int xruns = deviceManager.getXRunCount();
    if (xruns < 0)
    {
        stopTimer();   // CoreAudio liefert es; wenn nicht, ist Dauer-Polling sinnlos
        logLine ("XRun-Zähler nicht verfügbar auf diesem Backend — Monitor gestoppt.");
        return;
    }

    if (lastXRunCount < 0)          // erster Messwert nach Device-(Neu-)Start
    {
        lastXRunCount = xruns;
        return;
    }

    if (xruns > lastXRunCount)
    {
        juce::String line;
        line << juce::Time::getCurrentTime().toISO8601 (true)
             << "  DROPOUT +" << (xruns - lastXRunCount)
             << " (gesamt " << xruns << ")";

        if (auto* device = deviceManager.getCurrentAudioDevice())
            line << "  buffer=" << device->getCurrentBufferSizeSamples()
                 << " @" << (int) device->getCurrentSampleRate() << "Hz"
                 << "  cpu=" << juce::String (deviceManager.getCpuUsage() * 100.0, 1) << "%";

        if (getContext)
            line << "  |  " << getContext();

        logLine (line);
        lastXRunCount = xruns;
    }
}

void DropoutMonitor::logLine (const juce::String& line)
{
    auto f = getLogFile();
    f.getParentDirectory().createDirectory();
    f.appendText (line + "\n");
    DBG (line);
}
