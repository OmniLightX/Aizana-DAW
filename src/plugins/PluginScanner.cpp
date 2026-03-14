#include "PluginScanner.h"

// ---------------------------------------------------------------------------
// Background scan thread
// ---------------------------------------------------------------------------

class PluginScanThread : public juce::Thread
{
public:
    PluginScanThread (juce::PluginDirectoryScanner& scanner,
                      std::function<void(float)>    onProgress,
                      std::function<void()>          onComplete)
        : juce::Thread ("PluginScanThread")
        , scanner      (scanner)
        , onProgress   (std::move (onProgress))
        , onComplete   (std::move (onComplete))
    {}

    void run() override
    {
        juce::String pluginName;

        while (! threadShouldExit())
        {
            const bool morePlugins = scanner.scanNextFile (true, pluginName);

            if (onProgress)
                onProgress (scanner.getProgress());

            if (! morePlugins)
                break;
        }

        if (onComplete)
            onComplete();
    }

private:
    juce::PluginDirectoryScanner& scanner;
    std::function<void(float)>    onProgress;
    std::function<void()>          onComplete;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginScanThread)
};

// ---------------------------------------------------------------------------
// PluginScanner
// ---------------------------------------------------------------------------

PluginScanner::PluginScanner()
{
    // Register standard JUCE formats (VST3 on all platforms; AU on macOS only)
    formatManager.addDefaultFormats();

    // Register CLAP format using the direct C API (RESEARCH.md Pitfall 1:
    // the alpha juce_clap_hosting module is NOT used)
    formatManager.addFormat (new ClapPluginFormat());
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

    // Dead plugins file — tracks plugins that previously crashed during scanning
    // so the scanner can skip them on future runs
    const auto deadPluginsFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                     .getChildFile ("Cloudes")
                                     .getChildFile ("dead_plugins.txt");

    deadPluginsFile.getParentDirectory().createDirectory();

    // PluginDirectoryScanner scans one format at a time — iterate over all formats.
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* format = formatManager.getFormat (i);
        if (format == nullptr || ! format->canScanForPlugins())
            continue;

        juce::PluginDirectoryScanner scanner (
            knownPlugins,
            *format,
            getDefaultSearchPaths(),
            true,             // recursive search
            deadPluginsFile,
            true              // allowPluginsWhichRequireAsynchronousInstantiation
        );

        juce::String pluginName;
        while (scanner.scanNextFile (true, pluginName))
        {
            if (onProgress)
                onProgress (scanner.getProgress());
        }
    }

    if (onComplete)
        onComplete();
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
