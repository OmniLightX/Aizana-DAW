#pragma once

#include <atomic>
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

    /** Live progress snapshot delivered to the onProgress callback during a scan.
        Carries enough to drive a counter + current-item display, not just a bar. */
    struct ScanProgress
    {
        float        fraction        = 0.0f;  // overall [0..1] across all scannable formats
        int          numPluginsFound = 0;     // entries in KnownPluginList so far
        juce::String currentPlugin;           // name of the plugin just probed (may be empty)
        juce::String formatName;              // format currently being scanned (e.g. "VST3")
        int          formatIndex     = 0;     // 0-based index into the scannable formats
        int          numFormats      = 0;     // count of scannable formats
    };

    /**
     * Scan for plugins in the standard platform search paths.
     *
     * Runs on a background thread via out-of-process child process scanning
     * (RESEARCH.md Pattern 4 — prevents crashes from killing the host).
     *
     * If a cache file is set (setPluginListCacheFile), the partial list is
     * persisted periodically and at every format boundary, so an interrupted
     * scan is never lost and the next launch resumes (already-known plugins
     * are skipped by PluginDirectoryScanner).
     *
     * Callbacks:
     *   - onProgress(ScanProgress): called after each probed plugin
     *   - onComplete(): called once scanning finishes NORMALLY (not on cancel)
     *
     * IMPORTANT: both callbacks fire on the background scan thread.
     * If you need to update UI, use juce::MessageManager::callAsync() with a SafePointer.
     */
    void scanForPlugins (std::function<void(const ScanProgress&)> onProgress,
                         std::function<void()>                    onComplete);

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
    // Generated plugin library (Cloudes-generated CLAP plugins)
    // ---------------------------------------------------------------------------

    /**
     * Scan a directory of Cloudes-generated .clap plugins and add them to the
     * KnownPluginList, so they appear in the plugin browser / insert menu like any
     * other plugin AND survive restarts (the .clap files on disk are the source of
     * truth — call this at startup and after each generation, not the XML cache).
     *
     * Generated plugins are grouped under the "Cloudes" manufacturer. addType()
     * de-dupes by identifier, so repeat calls are safe and idempotent.
     *
     * @return number of NEW plugins added (0 if all were already known).
     */
    int registerGeneratedPlugins (const juce::File& generatedPluginsDir);

    /**
     * Rename a generated plugin's DISPLAY name (G5b — library management). The real name
     * is baked into the .clap by Faust, so the new name is stored in a sidecar
     * (.cloudes-names.json) in the generated dir and re-applied by registerGeneratedPlugins()
     * on every startup. Also updates the live KnownPluginList immediately so the browser
     * reflects it. Returns false if newDisplayName is blank or the .clap doesn't exist.
     */
    bool renameGeneratedPlugin (const juce::File& clapFile, const juce::String& newDisplayName);

    /**
     * Delete a generated plugin (G5b): remove it from the KnownPluginList, drop any name
     * override, and move its .clap to the Trash (recoverable). Returns false if the file
     * could not be removed.
     */
    bool deleteGeneratedPlugin (const juce::File& clapFile);

    /** The current display name for a generated .clap — its override if set, else the
     *  file name without extension (which is the plugin's name as compiled). */
    static juce::String generatedDisplayName (const juce::File& clapFile);

    // ---------------------------------------------------------------------------
    // Persistence — serialize/deserialize KnownPluginList to/from XML file
    // ---------------------------------------------------------------------------

    /**
     * Save the current KnownPluginList to an XML file.
     *
     * markComplete records whether this snapshot represents a fully finished
     * scan (true) or a partial/interrupted one (false). The flag is read back
     * by loadPluginList() so the app can decide whether to resume scanning.
     */
    void savePluginList (const juce::File& file, bool markComplete = true);

    /**
     * Load a previously saved KnownPluginList from an XML file.
     * Does not trigger a scan — it restores previously scanned results.
     * Also restores the "scan was complete" flag (see lastScanWasComplete()).
     */
    void loadPluginList (const juce::File& file);

    /**
     * Set the cache file used for incremental, crash-safe persistence DURING a
     * scan. When set, scanForPlugins() writes the partial list periodically and
     * at every format boundary — so an interrupted scan is never lost. Set this
     * once before scanning (e.g. at app startup).
     */
    void setPluginListCacheFile (const juce::File& file) { pluginListCacheFile = file; }

    /**
     * True if the most recently loaded or completed list represents a FULLY
     * finished scan. False means the cache is partial (the previous scan was
     * interrupted) and should be resumed. Defaults to true for a fresh/empty
     * list and for legacy caches without the marker.
     */
    bool lastScanWasComplete() const noexcept { return scanWasComplete.load(); }

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

    // Incremental persistence (crash-safe / resumable scans).
    juce::File                                           pluginListCacheFile;
    std::atomic<bool>                                    scanWasComplete { true };

    // Throttle for incremental saves during a scan.
    static constexpr int    kSaveEveryNPlugins = 20;     // save after this many new entries
    static constexpr double kSaveIntervalMs    = 4000.0; // ...or at least this often

    /** Returns the standard VST3/AU/CLAP search paths for the current platform. */
    static juce::FileSearchPath getDefaultSearchPaths();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginScanner)
};
