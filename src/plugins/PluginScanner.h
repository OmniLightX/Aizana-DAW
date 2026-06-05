#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ClapPluginFormat.h"

/**
 * PluginScanner — Out-of-process plugin scanner for VST3, AU, and CLAP formats.
 *
 * Uses JUCE's PluginDirectoryScanner which runs each plugin's testPlugin()
 * in a child process (RESEARCH.md Pattern 4). This prevents crashes from
 * misbehaving plugins from killing the host.
 *
 * Scan results are persisted to a KnownPluginList XML file so the host
 * does not need to re-scan on every startup.
 *
 * Usage:
 *   PluginScanner scanner;
 *   scanner.scanForPlugins ([](float f) { updateProgress(f); },
 *                           []()        { onScanComplete(); });
 *   const auto& knownPlugins = scanner.getKnownPlugins();
 */
class PluginScanner
{
public:
    PluginScanner();
    ~PluginScanner();

    // ---------------------------------------------------------------------------
    // Scanning
    // ---------------------------------------------------------------------------

    /**
     * Scan for plugins in the standard platform search paths.
     *
     * Runs on a background thread via out-of-process child process scanning
     * (RESEARCH.md Pattern 4 — prevents crashes from killing the host).
     *
     * Callbacks:
     *   - onProgress(fraction): called periodically with [0.0, 1.0] progress
     *   - onComplete(): called once scanning is finished (on the background thread)
     *
     * IMPORTANT: onComplete is called from the background thread.
     * If you need to update UI, use juce::MessageManager::callAsync().
     */
    void scanForPlugins (std::function<void(float)> onProgress,
                         std::function<void()>       onComplete);

    /**
     * Enable out-of-process (crash-isolated) scanning. Each plugin is then probed in a
     * child process, so a crashing/incompatible plugin blacklists only itself instead of
     * killing the scan or the app. Call once before scanForPlugins(). App-only: relies on
     * the scanner-worker entry point in the app's JUCEApplication::initialise().
     */
    void enableCrashIsolatedScanning();

    /** Cancel an in-progress scan. Blocks until the background thread stops. */
    void cancelScan();

    /** Block until the current scan finishes (or timeoutMs elapses). Useful in tests. */
    void waitForScanComplete (int timeoutMs = 30000);

    // ---------------------------------------------------------------------------
    // Results
    // ---------------------------------------------------------------------------

    const juce::KnownPluginList& getKnownPlugins() const { return knownPlugins; }
    juce::KnownPluginList& getKnownPlugins()              { return knownPlugins; }

    // ---------------------------------------------------------------------------
    // Persistence — serialize/deserialize KnownPluginList to/from XML file
    // ---------------------------------------------------------------------------

    /**
     * Save the current KnownPluginList to an XML file.
     * Call this after a scan completes to cache results across restarts.
     */
    void savePluginList (const juce::File& file);

    /**
     * Load a previously saved KnownPluginList from an XML file.
     * Does not trigger a scan — it restores previously scanned results.
     */
    void loadPluginList (const juce::File& file);

    // ---------------------------------------------------------------------------
    // Format manager access
    // ---------------------------------------------------------------------------

    /**
     * Returns the format manager with all registered formats (VST3, AU, CLAP).
     * Use this to instantiate plugins once they appear in KnownPluginList.
     */
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

private:
    juce::AudioPluginFormatManager                       formatManager;
    juce::KnownPluginList                                knownPlugins;
    std::unique_ptr<juce::PluginDirectoryScanner>        activeScanner;
    std::unique_ptr<juce::Thread>                        scanThread;

    /** Returns the standard VST3/AU/CLAP search paths for the current platform. */
    static juce::FileSearchPath getDefaultSearchPaths();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginScanner)
};
