#include <juce_gui_extra/juce_gui_extra.h>
#include "MainWindow.h"
#include "MicrophonePermission.h"
#include "LegacyMigration.h"
#include "engine/EditManager.h"
#include "plugins/OutOfProcessPluginScanner.h"
#include "plugins/oop/OutOfProcessPluginInstance.h"
#include "testing/UiTestHook.h"
#include "BinaryData.h"

//==============================================================================
/**
 * AizanaApplication — JUCE application entry point for the Aizana DAW.
 *
 * Phase 1 skeleton. Tracktion Engine initialisation, audio device setup,
 * and session management are added in plan 01-02 (Engine Core).
 */
class AizanaApplication : public juce::JUCEApplication
{
public:
    AizanaApplication() = default;

    const juce::String getApplicationName() override       { return "Aizana"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }

    // Normally single-instance — but the out-of-process plugin scanner relaunches this
    // same binary as a worker (its command line carries the scanner id). That second
    // launch must be allowed through the single-instance lock, or scanning deadlocks.
    bool moreThanOneInstanceAllowed() override
    {
        const auto cmd = getCommandLineParameters();
        return cmd.contains (kScannerProcessUID)
            || cmd.contains (aizana::kPluginHostProcessUID);   // out-of-process plugin host worker
    }

    /** Erste Session-Datei aus der Kommandozeile (Programmstart mit Argument,
        `open -a Aizana song.tracktionedit`, Doppelklick im Finder). */
    static bool istSession (const juce::File& f)
    {
        return f.existsAsFile() && (f.hasFileExtension ("tracktionedit")
                                    || f.hasFileExtension ("tracktion"));
    }

    static juce::File sessionFileFrom (const juce::String& commandLine)
    {
        // Erst die ZEILE ALS GANZES pruefen: JUCE reicht die Argumente als einen
        // String durch, und ein Sessionpfad enthaelt regelmaessig Leerzeichen
        // ("Unsaved Recordings"). Zuerst zu zerlegen zerriss genau solche Pfade —
        // die Datei wurde still ignoriert und die App zeigte die leere Startsession.
        const auto ganz = commandLine.unquoted().trim();

        if (istSession (juce::File (juce::File::isAbsolutePath (ganz)
                                        ? ganz
                                        : juce::File::getCurrentWorkingDirectory()
                                              .getChildFile (ganz).getFullPathName())))
            return juce::File (juce::File::isAbsolutePath (ganz)
                                   ? ganz
                                   : juce::File::getCurrentWorkingDirectory()
                                         .getChildFile (ganz).getFullPathName());

        // Sonst die einzelnen Argumente — auch mehrere, davon eines eine Session.
        auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
        args.addArray (juce::StringArray::fromTokens (commandLine, true));

        for (const auto& tok : args)
        {
            const juce::File f (juce::File::isAbsolutePath (tok)
                                    ? tok
                                    : juce::File::getCurrentWorkingDirectory()
                                          .getChildFile (tok).getFullPathName());

            if (istSession (f))
                return f;
        }

        return {};
    }

    /** Session oeffnen, sobald das Fenster steht. */
    void openPendingSession()
    {
        if (mainWindow != nullptr && pendingSession.existsAsFile())
        {
            mainWindow->openSession (pendingSession);
            pendingSession = juce::File();
        }
    }

    void initialise (const juce::String& commandLine) override
    {
        pendingSession = sessionFileFrom (commandLine);

        // If this process was launched as a plugin-scan worker, run ONLY that — no
        // window, no audio engine, no mic prompt. Keep the handle alive for its lifetime.
        if (auto worker = startScannerSubprocessIfRequested (commandLine))
        {
            scannerWorker = std::move (worker);
            return;
        }

        // Out-of-process plugin host worker (E5.3b): this process hosts ONE real plugin for the
        // main app and pumps its audio over a SharedAudioRing. Load via a fresh format manager
        // (CLAP/VST3/AU/Built-in). Like the scanner worker: no window, no engine, no mic prompt.
        if (auto host = aizana::startPluginHostWorkerIfRequested (commandLine,
                [] (const juce::PluginDescription& d, double rate, int block, juce::String& err)
                    -> std::unique_ptr<juce::AudioProcessor>
                {
                    juce::AudioPluginFormatManager fm;
                    registerScannableFormats (fm);

                    if (auto inst = fm.createPluginInstance (d, rate, block, err))
                        return inst;

                    // The description may be OOP-wrapped (pluginFormatName == kOopFormatName),
                    // which fm doesn't know. Resolve the REAL plugin by its file path instead.
                    for (auto* f : fm.getFormats())
                        if (f != nullptr && f->fileMightContainThisPluginType (d.fileOrIdentifier))
                        {
                            juce::OwnedArray<juce::PluginDescription> found;
                            f->findAllTypesForFile (found, d.fileOrIdentifier);
                            if (! found.isEmpty())
                                if (auto inst = fm.createPluginInstance (*found.getFirst(), rate, block, err))
                                    return inst;
                        }
                    return nullptr;
                }))
        {
            pluginHostWorker = std::move (host);
            return;
        }

        // Rename-Migration Cloudes→Aizana: MUST run before MainWindow constructs the
        // te::Engine — the engine creates ~/Library/Aizana on construction, and the
        // merge-move of ~/Library/Cloudes (settings, plugin library, models, training)
        // has to happen first. Idempotent no-op once migrated.
        aizana::legacy::migrateAppData();

       #if AIZANA_UITEST_HOOK
        // GUI-Testlauf (nur in einem Build mit -DAIZANA_UITEST_HOOK=ON vorhanden).
        // armIfRequested() IST das Tor: es gibt nur dann etwas zurueck, wenn
        // AIZANA_UITEST=1 gesetzt UND stdin eine unbenannte Pipe ist.
        //
        // Zuerst scharf machen, DANN das Fenster bauen: MainComponent fragt im
        // Konstruktor isArmed() ab, um alles abzuschalten, was aus dem Prozess
        // hinausreicht (Loopback-Server der KI-Bruecke). Umgekehrte Reihenfolge
        // haette den Server bereits gestartet.
        //
        // Im Testmodus zusaetzlich weggelassen: der Splash (eigenes Fenster =
        // verschobene Peer-Indizes) und die Mikrofon-Abfrage (ein nativer
        // System-Dialog, den der Hook konstruktionsbedingt nicht bedienen kann,
        // und der nach jedem Rebuild wiederkommt, weil die Ad-hoc-Signatur wechselt).
        if ((uiTestHook = aizana::uitest::Hook::armIfRequested()) != nullptr)
        {
            juce::MessageManager::callAsync ([this]
            {
                mainWindow = std::make_unique<MainWindow> (getApplicationName());
                openPendingSession();

                if (mainWindow != nullptr)
                    mainWindow->offerCrashRecovery();
            });
            return;
        }
       #endif

        // Brand splash — show the Aizana logo while the engine + window load.
        // SplashScreen self-owns and self-deletes after the delay.
        if (auto logo = juce::ImageCache::getFromMemory (BinaryData::AizanaIcon_png,
                                                         BinaryData::AizanaIcon_pngSize);
            logo.isValid())
        {
            auto* splash = new juce::SplashScreen ("Aizana",
                logo.rescaled (300, 300, juce::Graphics::highResamplingQuality), true);
            splash->deleteAfterDelay (juce::RelativeTime::seconds (2.2), false);
        }

        // Defer permission request to the first run-loop tick so NSApplication
        // is fully active before AVCaptureDevice shows the system dialog.
        juce::MessageManager::callAsync ([this]
        {
            requestMicrophonePermission ([this] (bool /*granted*/)
            {
                mainWindow = std::make_unique<MainWindow> (getApplicationName());
                openPendingSession();

                // Erst NACH dem Oeffnen einer eventuell mitgegebenen Session
                // fragen — sonst ueberschriebe die Wiederherstellung sie sofort.
                if (mainWindow != nullptr)
                    mainWindow->offerCrashRecovery();
            });
        });
    }

    void shutdown() override
    {
        // SAUBER beendet — die Marke der offenen Sitzung faellt weg. Ihr
        // VORHANDENSEIN beim naechsten Start ist der einzige Unterschied
        // zwischen „abgestuerzt" und „normal zugemacht"; genau daran haengt die
        // Nachfrage nach der Wiederherstellung.
        EditManager::clearCrashMarker();
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        // Gedacht fuer den Doppelklick im Finder auf eine laufende Instanz.
        // MESSSTAND 2026-08-13: dieser Rueckruf feuert auf diesem System NICHT —
        // mit einer Marker-Datei nachgewiesen, auch nach dem Deklarieren der
        // Dokumenttypen (DOCUMENT_EXTENSIONS) und lsregister. macOS liefert das
        // Oeffnen-Ereignis also gar nicht aus. Der Code bleibt stehen, weil er
        // richtig ist, sobald das Ereignis kommt; BELEGT ist nur der Weg ueber die
        // Kommandozeile (Start mit Dateipfad).
        pendingSession = sessionFileFrom (commandLine);
        openPendingSession();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
    juce::File                  pendingSession;   // aus der Kommandozeile/vom Finder
    std::shared_ptr<void>       scannerWorker;      // alive only in the scan-worker subprocess
    std::shared_ptr<aizana::PluginHostWorkerHandle> pluginHostWorker;   // alive only in a plugin-host worker

   #if AIZANA_UITEST_HOOK
    std::unique_ptr<aizana::uitest::Hook> uiTestHook;                  // nur im UI-Test-Build vorhanden
   #endif
};

//==============================================================================
// JUCE application entry point macro
START_JUCE_APPLICATION (AizanaApplication)
