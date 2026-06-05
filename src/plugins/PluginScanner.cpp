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

void PluginScanner::scanForPlugins (std::function<void(float)> onProgress,
                                    std::function<void()>       onComplete)
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
        DBG ("PluginScanner: scanning " + juce::String (formatManager.getNumFormats()) + " formats");
        for (int i = 0; i < formatManager.getNumFormats(); ++i)
        {
            auto* format = formatManager.getFormat (i);
            if (format == nullptr || ! format->canScanForPlugins())
                continue;

            DBG ("  Format: " + format->getName()
                 + " canScan=" + juce::String (format->canScanForPlugins() ? "yes" : "no"));

            const auto paths = getDefaultSearchPaths();
            DBG ("  Search paths: " + paths.toString());

            juce::PluginDirectoryScanner scanner (
                knownPlugins,
                *format,
                paths,
                true,             // recursive search
                deadPluginsFile,
                true              // allowPluginsWhichRequireAsynchronousInstantiation
            );

            juce::String pluginName;
            try
            {
                while (! (scanThread != nullptr && scanThread->threadShouldExit()))
                {
                    const bool more = scanner.scanNextFile (true, pluginName);
                    DBG ("  Scanned: " + pluginName);
                    if (onProgress)
                        onProgress (scanner.getProgress());
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
        }

        DBG ("PluginScanner: scan complete — "
             + juce::String (knownPlugins.getNumTypes()) + " plugins found");

        if (onComplete)
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
// Persistence
// ---------------------------------------------------------------------------

void PluginScanner::savePluginList (const juce::File& file)
{
    if (const auto xml = knownPlugins.createXml())
    {
        file.getParentDirectory().createDirectory();
        xml->writeTo (file);
    }
}

void PluginScanner::loadPluginList (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    if (const auto xml = juce::XmlDocument::parse (file))
        knownPlugins.recreateFromXml (*xml);
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
