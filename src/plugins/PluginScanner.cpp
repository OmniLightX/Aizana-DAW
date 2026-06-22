#include "PluginScanner.h"
#include "BuiltInPluginFormat.h"
#include "OutOfProcessPluginScanner.h"

// ---------------------------------------------------------------------------
// Background scan thread (multi-format, self-contained)
// ---------------------------------------------------------------------------

class ScanThread : public juce::Thread
{
public:
    std::function<void()> body;
    ScanThread() : juce::Thread ("PluginScanThread") {}
    void run() override { if (body) body(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScanThread)
};

// ---------------------------------------------------------------------------
// PluginScanner
// ---------------------------------------------------------------------------

PluginScanner::PluginScanner()
{
    // VST3 (+ AU on macOS) + CLAP (direct C API; the alpha juce_clap_hosting module is
    // NOT used) + BuiltInPluginFormat (so TE's ExternalPlugin can instantiate our own FX
    // via the normal createPluginInstance path). Shared with the scanner subprocess so
    // format names match across the process boundary.
    registerScannableFormats (formatManager);

    // Pre-populate KnownPluginList with the 4 built-in FX descriptions.
    // This makes them appear in PluginBrowserPopup immediately without scanning.
    juce::OwnedArray<juce::PluginDescription> builtIns;
    BuiltInPluginFormat::getBuiltInDescriptions (builtIns);
    for (auto* desc : builtIns)
        knownPlugins.addType (*desc);
}

void PluginScanner::enableCrashIsolatedScanning()
{
    // Fix 4: probe every third-party plugin in a child process, so a broken plugin can't
    // kill the scan or the app. The existing scanNextFile() loop routes through this
    // automatically; a crash blacklists the one plugin and the scan continues. Opt-in
    // (called by the app, not by tests) because the worker entry point lives in the app's
    // JUCEApplication::initialise — the test binary has no worker to relaunch into.
    installOutOfProcessScanner (knownPlugins);
}

PluginScanner::~PluginScanner()
{
    cancelScan();
}

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

void PluginScanner::scanForPlugins (std::function<void(const ScanProgress&)> onProgress,
                                    std::function<void()>                    onComplete)
{
    // Cancel any in-progress scan first
    cancelScan();

    const auto deadPluginsFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                     .getChildFile ("Cloudes")
                                     .getChildFile ("dead_plugins.txt");
    deadPluginsFile.getParentDirectory().createDirectory();

    // Build the scan body: iterates all formats sequentially on a background thread.
    // PluginDirectoryScanner is not thread-safe — one scanner at a time.
    auto scanBody = [this, deadPluginsFile,
                     onProgress = std::move (onProgress),
                     onComplete = std::move (onComplete)]()
    {
        // Collect the formats that can actually be scanned (VST3, AU, CLAP, ...).
        // Iterating these directly gives a stable 0-based index for progress reporting.
        juce::Array<juce::AudioPluginFormat*> scannable;
        for (int i = 0; i < formatManager.getNumFormats(); ++i)
            if (auto* f = formatManager.getFormat (i); f != nullptr && f->canScanForPlugins())
                scannable.add (f);

        const int numFormats = scannable.size();
        scanWasComplete.store (false);   // in progress until proven complete

        DBG ("PluginScanner: scanning " + juce::String (numFormats) + " scannable formats");

        for (int fi = 0; fi < numFormats; ++fi)
        {
            auto* format = scannable.getUnchecked (fi);
            const auto paths = getDefaultSearchPaths();
            DBG ("  Format: " + format->getName() + " — paths: " + paths.toString());

            juce::PluginDirectoryScanner scanner (
                knownPlugins,
                *format,
                paths,
                true,             // recursive search
                deadPluginsFile,
                true              // allowPluginsWhichRequireAsynchronousInstantiation
            );

            juce::String pluginName;
            double lastSaveMs     = juce::Time::getMillisecondCounterHiRes();
            int    lastSavedCount = knownPlugins.getNumTypes();

            try
            {
                while (! (scanThread != nullptr && scanThread->threadShouldExit()))
                {
                    const bool more  = scanner.scanNextFile (true, pluginName);
                    const int  found = knownPlugins.getNumTypes();
                    DBG ("  Scanned: " + pluginName);

                    if (onProgress)
                    {
                        ScanProgress p;
                        p.fraction        = numFormats > 0
                                              ? (float) ((fi + scanner.getProgress()) / numFormats)
                                              : 0.0f;
                        p.numPluginsFound = found;
                        p.currentPlugin   = pluginName;
                        p.formatName      = format->getName();
                        p.formatIndex     = fi;
                        p.numFormats      = numFormats;
                        onProgress (p);
                    }

                    // Throttled incremental save — keeps the scan crash-safe AND
                    // resumable: PluginDirectoryScanner skips already-known files on
                    // the next launch, so a partial cache means "continue from here".
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (pluginListCacheFile != juce::File()
                        && (found - lastSavedCount >= kSaveEveryNPlugins
                            || now - lastSaveMs >= kSaveIntervalMs))
                    {
                        savePluginList (pluginListCacheFile, false);
                        lastSaveMs     = now;
                        lastSavedCount = found;
                    }

                    if (! more)
                        break;
                }
            }
            catch (const std::exception& ex)
            {
                DBG ("PluginScanner: exception during scan of "
                     + format->getName() + ": " + juce::String (ex.what()));
            }
            catch (...)
            {
                DBG ("PluginScanner: unknown exception during scan of " + format->getName());
            }

            // Persist at every format boundary so a whole format's results survive
            // even if the next format hangs and triggers a watchdog kill.
            if (pluginListCacheFile != juce::File())
                savePluginList (pluginListCacheFile, false);
        }

        const bool cancelled = (scanThread != nullptr && scanThread->threadShouldExit());
        scanWasComplete.store (! cancelled);

        DBG ("PluginScanner: scan " + juce::String (cancelled ? "cancelled" : "complete")
             + " — " + juce::String (knownPlugins.getNumTypes()) + " plugins found");

        // Final authoritative save carrying the correct complete/partial marker.
        if (pluginListCacheFile != juce::File())
            savePluginList (pluginListCacheFile, ! cancelled);

        // onComplete fires only for a NORMAL finish — a cancelled scan must not
        // trigger "scan complete" UI (alerts, sync, etc.).
        if (! cancelled && onComplete)
            onComplete();
    };

    // Launch on background thread — returns immediately (non-blocking).
    // scanThread is stored so cancelScan() can stop it cleanly.
    auto* thread = new ScanThread();
    thread->body = std::move (scanBody);
    scanThread.reset (thread);
    thread->startThread (juce::Thread::Priority::background);
}

void PluginScanner::waitForScanComplete (int timeoutMs)
{
    if (scanThread != nullptr)
        scanThread->waitForThreadToExit (timeoutMs);
}

void PluginScanner::cancelScan()
{
    if (scanThread != nullptr)
    {
        scanThread->signalThreadShouldExit();
        scanThread->waitForThreadToExit (5000);
        scanThread.reset();
    }
    activeScanner.reset();
}

// ---------------------------------------------------------------------------
// Generated plugin library
// ---------------------------------------------------------------------------

int PluginScanner::registerGeneratedPlugins (const juce::File& dir)
{
    if (! dir.isDirectory())
        return 0;

    // A local format instance is enough to read the descriptions — findAllTypesForFile
    // dlopens/scans/dlcloses each .clap on its own (no dependency on formatManager state).
    ClapPluginFormat clap;
    int added = 0;

    for (const auto& file : dir.findChildFiles (juce::File::findFiles, false, "*.clap"))
    {
        juce::OwnedArray<juce::PluginDescription> descs;
        clap.findAllTypesForFile (descs, file.getFullPathName());

        for (auto* d : descs)
        {
            d->manufacturerName = "Cloudes";   // group as the Cloudes plugin library
            if (knownPlugins.addType (*d))     // de-dupes by identifier (file path)
                ++added;
        }
    }

    return added;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void PluginScanner::savePluginList (const juce::File& file, bool markComplete)
{
    if (const auto xml = knownPlugins.createXml())
    {
        // Stamp the resume marker on the root element. KnownPluginList::recreateFromXml()
        // only reads PLUGIN child elements, so JUCE ignores this attribute on load; we
        // read it back explicitly in loadPluginList() to decide whether to resume.
        xml->setAttribute ("cloudesScanComplete", markComplete ? 1 : 0);
        file.getParentDirectory().createDirectory();
        xml->writeTo (file);
    }
}

void PluginScanner::loadPluginList (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    if (const auto xml = juce::XmlDocument::parse (file))
    {
        knownPlugins.recreateFromXml (*xml);
        // Legacy caches without the marker are treated as complete to avoid a
        // surprise full re-scan for existing users.
        scanWasComplete.store (xml->getBoolAttribute ("cloudesScanComplete", true));
    }
}

// ---------------------------------------------------------------------------
// Default search paths
// ---------------------------------------------------------------------------

juce::FileSearchPath PluginScanner::getDefaultSearchPaths()
{
    juce::FileSearchPath paths;

#if JUCE_MAC
    // System-wide VST3
    paths.add (juce::File ("/Library/Audio/Plug-Ins/VST3"));
    // User-specific VST3
    paths.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Audio/Plug-Ins/VST3"));

    // System-wide CLAP
    paths.add (juce::File ("/Library/Audio/Plug-Ins/CLAP"));
    // User-specific CLAP
    paths.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Audio/Plug-Ins/CLAP"));

    // AU scanning is handled by JUCE's AudioUnitPluginFormat internally —
    // it reads from the AudioComponent registry, not from the filesystem.
    // No filesystem path is needed for AU.

#elif JUCE_WINDOWS
    // System-wide VST3
    paths.add (juce::File ("C:\\Program Files\\Common Files\\VST3"));
    // Per-user VST3
    paths.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("VST3"));

    // System-wide CLAP
    paths.add (juce::File ("C:\\Program Files\\Common Files\\CLAP"));
    // Per-user CLAP
    paths.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("CLAP"));

#else
    // Linux — XDG standard paths
    paths.add (juce::File ("/usr/lib/vst3"));
    paths.add (juce::File ("/usr/local/lib/vst3"));
    paths.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile (".vst3"));
    paths.add (juce::File ("/usr/lib/clap"));
    paths.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile (".clap"));
#endif

    return paths;
}
