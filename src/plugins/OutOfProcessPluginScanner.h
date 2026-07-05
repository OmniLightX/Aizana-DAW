#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

/**
 * Out-of-process plugin scanning (Fix 4, 2026-06-05).
 *
 * Each third-party plugin is probed in a short-lived CHILD PROCESS — a second launch
 * of this same executable in "scanner worker" mode. If a plugin crashes or hangs the
 * probe, only the child dies: the coordinator (the running app) detects the lost
 * connection, blacklists that one plugin, and the scan continues. A single broken
 * plugin can no longer take down the whole scan or the app (Studio One behaviour).
 * Previously the scan ran in-process, so one bad plugin killed the app and the user
 * could never finish scanning their library.
 *
 * Two halves, both compiled into the Aizana binary:
 *   - Coordinator: installOutOfProcessScanner() registers a KnownPluginList::CustomScanner.
 *     The existing PluginDirectoryScanner loop then routes every probe through a subprocess
 *     automatically (KnownPluginList::scanAndAddFile blacklists on a false return).
 *   - Worker: startScannerSubprocessIfRequested() — call it first thing in
 *     JUCEApplication::initialise(). If the command line marks this process as a scan
 *     worker it returns a live handle (keep it alive); the app must then do nothing else.
 */

// Shared handshake token between coordinator and worker. Also used to let the worker
// process through the app's single-instance lock (its command line contains this id).
inline constexpr const char* kScannerProcessUID = "aizana_plugin_scan";

/** Register the disk-scannable plugin formats (VST3 / AU / CLAP / Built-in). Used on
 *  BOTH sides so the coordinator's format name always matches a worker format. */
void registerScannableFormats (juce::AudioPluginFormatManager&);

/** Install out-of-process scanning on a KnownPluginList (coordinator side). */
void installOutOfProcessScanner (juce::KnownPluginList&);

/** If `commandLine` marks this process as a plugin-scan worker, start it and return an
 *  opaque handle that must be kept alive for the process lifetime; otherwise nullptr.
 *  (shared_ptr<void> so callers need not see the worker's definition.) */
std::shared_ptr<void> startScannerSubprocessIfRequested (const juce::String& commandLine);
