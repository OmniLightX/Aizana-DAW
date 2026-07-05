#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

/**
 * DropoutMonitor — M13-Diagnose: protokolliert Audio-Dropouts (XRuns) mit Kontext.
 *
 * Der User hört sporadische Komplett-Aussetzer beim Playback/Mixing; die Ursache
 * ist nicht bestätigt (Autosave ist seit dem playback-safe Snapshot raus,
 * Verdacht #1: Streaming/mmap von einem externen Volume, vgl. M12-SIGBUS).
 * Dieser Monitor macht jeden Aussetzer beweisbar: er pollt sekündlich
 * juce::AudioDeviceManager::getXRunCount() und schreibt bei jedem Anstieg eine
 * Zeile mit Zeitstempel, CPU-Last, Buffergröße/Samplerate und App-Kontext
 * (Transport, Session-Volume, letzter Autosave) nach
 * ~/Library/Aizana/logs/dropouts.log.
 *
 * Nur Message-Thread + billige Reads — greift NIE in den Audio-Pfad ein.
 */
class DropoutMonitor : private juce::Timer
{
public:
    /** contextProvider wird nur bei einem erkannten Dropout aufgerufen und darf
        beliebige App-Infos liefern (Transport-Position, Session-Pfad, ...). */
    DropoutMonitor (juce::AudioDeviceManager& adm,
                    std::function<juce::String()> contextProvider);
    ~DropoutMonitor() override;

    /** Log-Datei (für Doku/Support-Hinweise). */
    static juce::File getLogFile();

private:
    void timerCallback() override;
    void logLine (const juce::String& line);

    juce::AudioDeviceManager& deviceManager;
    std::function<juce::String()> getContext;
    int lastXRunCount = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DropoutMonitor)
};
