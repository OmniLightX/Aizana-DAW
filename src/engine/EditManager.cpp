#include "EditManager.h"
#include "WarpEngine.h"
#include "app/LegacyMigration.h"
#include "plugins/BuiltInPluginFormat.h"
#include "plugins/ClapPluginFormat.h"
#include "RemovableMediaGuard.h"

// Convenience aliases for Tracktion's strong time types
using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;

//==============================================================================
EditManager::EditManager()
{
    // Register BuiltInPluginFormat with TE's own AudioPluginFormatManager
    // so ExternalPlugin can instantiate built-in FX processors.
    // This MUST happen before any Edit is created (i.e., before createNewEdit / loadEdit).
    engine.getPluginManager().pluginFormatManager.addFormat (new BuiltInPluginFormat());

    // Register CLAP with the ENGINE's format manager so ExternalPlugin (and thus
    // PluginManager::insertPlugin) can instantiate .clap plugins — both 3rd-party CLAPs
    // and the Aizana-generated ones (Phase 4 / Iteration B). The scanner has its own
    // ClapPluginFormat; the engine needs its own to host them.
    engine.getPluginManager().pluginFormatManager.addFormat (new ClapPluginFormat());

    // Also add built-in descriptions to TE's knownPluginList so ExternalPlugin::findMatchingPlugin()
    // can locate them. TE's ExternalPlugin::doFullInitialisation() searches engine.getPluginManager().knownPluginList
    // — which is a separate object from PluginScanner::knownPlugins. Without this, getAudioPluginInstance()
    // returns null and FloatingPluginWindow shows "No editor available".
    juce::OwnedArray<juce::PluginDescription> builtIns;
    BuiltInPluginFormat::getAllBuiltInDescriptions (builtIns);   // FX + instruments
    for (auto* desc : builtIns)
        engine.getPluginManager().knownPluginList.addType (*desc);

    // M12: Dateien auf externen/Netz-Volumes NIE memory-mappen — ein gezogenes
    // Volume machte gemappte Seiten ungültig → SIGBUS (CacheRefresher/Audio-Thread).
    // TE fällt für diese Dateien auf seinen Buffered-Reader-Pfad zurück.
    aizana::installRemovableMediaGuard (engine);
}
EditManager::~EditManager() = default;

//==============================================================================
void EditManager::createNewEdit()
{
    // Create a fresh Edit with a single audio track.
    // The engine owns the Edit's UndoManager — all subsequent mutations route
    // through edit->getUndoManager() (the SINGLE shared undo manager per CONTEXT.md).
    auto newEdit = te::Edit::createSingleTrackEdit (engine);

    // Let UI owners drop every reference into the old Edit WHILE IT IS STILL
    // ALIVE (listeners, track-bound components, plugin windows) — destroying
    // it first left views pointing at freed tracks → crash on File>New/Open.
    if (edit != nullptr)
    {
        edit->getTransport().stop (false, false);
        warpRenderWatcher.detach();
        if (onEditWillChange)
            onEditWillChange();
    }

    edit = std::move (newEdit);
    warpRenderWatcher.attach (*edit);

    // Tracktion's WaveInputDevice records to "%projectdir%" and falls back to
    // juce::File::getCurrentWorkingDirectory() if an Edit has no file/project.
    // A macOS app launched from Finder can have an unwritable working directory,
    // causing recording contexts to fail before any WaveAudioClip is created.
    // Give unsaved sessions a stable writable media root immediately.
    auto unsavedDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                          .getChildFile ("Aizana")
                          .getChildFile ("Unsaved Recordings");
    if (! unsavedDir.createDirectory())
        unsavedDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("Aizana Unsaved Recordings");
    unsavedDir.createDirectory();

    currentSessionFile = unsavedDir.getChildFile ("Untitled.tracktionedit");
    unsavedDir.setAsCurrentWorkingDirectory();

    // The session file must EXIST on disk even before the first save:
    // TE stores relative source refs (e.g. loop-take files) via
    // File::getRelativePathFrom (editFileRetriever()) — and JUCE only treats
    // the base as a file when it exists, otherwise as a directory, which bakes
    // one extra "../" into every stored take path (= unresolvable sources).
    if (! currentSessionFile.existsAsFile())
        currentSessionFile.create();

    if (edit != nullptr)
    {
        const auto editFile = currentSessionFile;
        edit->editFileRetriever = [editFile] { return editFile; };

        // filePathResolver is required alongside editFileRetriever.
        // TE's SourceFileReference::findFileFromString() uses filePathResolver to resolve
        // stored path strings back to juce::File objects. Without it, TE falls back to
        // getEditFileFromProjectManager() which returns an empty File in sessions without
        // a proper ProjectManager — causing getFile() to return an invalid path for ALL
        // clips (initial import AND split clips), breaking both waveform display and audio
        // playback. Handles both absolute paths (passthrough) and relative paths (resolve
        // from Edit's parent directory).
        edit->filePathResolver = [editFile] (const juce::String& desc) -> juce::File
        {
            if (juce::File::isAbsolutePath (desc)) return juce::File (desc);
            auto resolved = editFile.getParentDirectory().getChildFile (desc);
            if (resolved.existsAsFile())
                return resolved;
            // Legacy refs stored while the session file did not yet exist on
            // disk: JUCE then treated the edit-file path as a directory, so the
            // stored path carries one extra "../" — re-base to compensate.
            auto legacy = editFile.getChildFile (desc);
            if (legacy.existsAsFile())
                return legacy;
            return resolved;
        };
    }

    // Ensure transport is stopped (it is by default, but be explicit)
    edit->getTransport().stop (false, false);

    // Disable TE's built-in click track. Aizana uses MetronomeGenerator instead —
    // a custom synthesiser that runs in AudioEngine::audioDeviceIOCallbackWithContext.
    // TE's click track defaults to false for fresh edits, but saved edit state or
    // future TE defaults could enable it, resulting in two simultaneous click generators.
    edit->clickTrackEnabled = false;

    // Allocate the playback context immediately so getAllInputDevices() returns live
    // InputDeviceInstance objects. Without this, getAllInputDevices() → getCurrentPlaybackContext()
    // returns null → empty array → assignInput / armClicked / refreshInputButton all see
    // "No Input" even when WaveInputDevices exist. TE's own examples always call this before
    // doing anything with input devices (EngineInPluginDemo, RecordingDemo).
    edit->getTransport().ensureContextAllocated();

    if (onEditChanged)
        onEditChanged();
}

//==============================================================================
bool EditManager::loadEdit (const juce::File& file)
{
    // Rename-Migration Cloudes→Aizana: upgrade pre-rename sessions in place BEFORE
    // TE parses them — old files carry cloudes* track properties, built-in plugin
    // identifiers whose uniqueId hash changed with the rename, and absolute paths
    // into ~/Library/Cloudes. Atomic write, no-op for post-rename sessions.
    aizana::legacy::migrateEditFile (file);

    // Use the Tracktion Engine helper that handles legacy format conversion.
    // loadEditFromFile() parses the XML, updates legacy state if needed, and
    // returns a fully constructed Edit ready for editing.
    //
    // Load into a LOCAL first: on failure the current Edit must stay intact
    // (returning with edit == nullptr would strand the whole UI), and on
    // success the old Edit must only die AFTER onEditWillChange let the UI
    // detach from it.
    auto newEdit = te::loadEditFromFile (engine, file, te::Edit::forEditing);

    if (newEdit == nullptr)
        return false;   // file missing/invalid — caller shows the error

    if (edit != nullptr)
    {
        edit->getTransport().stop (false, false);
        warpRenderWatcher.detach();
        if (onEditWillChange)
            onEditWillChange();
    }

    edit = std::move (newEdit);
    warpRenderWatcher.attach (*edit);

    // Same as createNewEdit: allocate context so input devices are immediately accessible.
    edit->getTransport().ensureContextAllocated();

    // TE's default filePathResolver (sibling-based) lacks the legacy fallback for
    // take refs recorded in unsaved sessions (see createNewEdit) — install ours.
    setCurrentFile (file);
    const auto editFile = file;
    edit->editFileRetriever = [editFile] { return editFile; };
    edit->filePathResolver = [editFile] (const juce::String& desc) -> juce::File
    {
        if (juce::File::isAbsolutePath (desc)) return juce::File (desc);
        auto resolved = editFile.getParentDirectory().getChildFile (desc);
        if (resolved.existsAsFile())
            return resolved;
        auto legacy = editFile.getChildFile (desc);
        if (legacy.existsAsFile())
            return legacy;
        return resolved;
    };

    if (onEditChanged)
        onEditChanged();

    return true;
}

//==============================================================================
void EditManager::saveEdit (const juce::File& file)
{
    if (edit == nullptr)
    {
        jassertfalse;
        return;
    }

    // Flush pending state changes into the ValueTree before serialising
    edit->flushState();

    // Use EditFileOperations for the canonical save path — handles the .tracktion format
    // that Tracktion Engine reloads correctly.
    te::EditFileOperations (*edit).saveAs (file, true);
}

//==============================================================================
void EditManager::beginAITransaction (const juce::String& description)
{
    if (edit == nullptr)
    {
        jassertfalse;
        return;
    }

    // All mutations between this call and the next beginNewTransaction() call
    // collapse into a single undo step — this is the AI batch undo requirement
    // from CONTEXT.md. Do NOT call beginNewTransaction() again during the batch.
    edit->getUndoManager().beginNewTransaction (description);
}

//==============================================================================
void EditManager::undoLastAction()
{
    if (edit != nullptr)
        edit->getUndoManager().undo();
}

void EditManager::redoLastAction()
{
    if (edit != nullptr)
        edit->getUndoManager().redo();
}

bool EditManager::canUndo() const
{
    return edit != nullptr && edit->getUndoManager().canUndo();
}

bool EditManager::canRedo() const
{
    return edit != nullptr && edit->getUndoManager().canRedo();
}

//==============================================================================
juce::UndoManager& EditManager::getUndoManager()
{
    jassert (edit != nullptr);
    return edit->getUndoManager();
}

//==============================================================================
// Autosave

void EditManager::setCurrentFile (const juce::File& file)
{
    currentSessionFile = file;

    if (file.existsAsFile())
    {
        // Start autosave timer on a valid session file
        // 0 Minuten = aus; sonst der eingestellte Abstand.
        if (const int mins = getAutosaveIntervalMinutes(); mins > 0)
            autosaveTimer.startTimer (mins * 60 * 1000);
        else
            autosaveTimer.stopTimer();
    }
    else
    {
        autosaveTimer.stopTimer();
    }
}

void EditManager::performAutosave()
{
    if (edit == nullptr || ! currentSessionFile.existsAsFile())
        return;

    // Build autosave filename: {SessionName}_autosave_N.tracktionedit
    // We rotate 3 slots: _autosave_1, _autosave_2, _autosave_3
    const auto stem = currentSessionFile.getFileNameWithoutExtension();
    const auto dir  = currentSessionFile.getParentDirectory();

    // Find the next slot (rotate among kAutosaveKeptCount slots)
    // The slot with the oldest modification time gets overwritten.
    juce::File oldestFile;
    juce::Time oldestTime = juce::Time::getCurrentTime();
    bool allExist = true;

    for (int i = 1; i <= kAutosaveKeptCount; ++i)
    {
        const auto candidate = dir.getChildFile (
            stem + "_autosave_" + juce::String (i) + ".tracktionedit");

        if (! candidate.existsAsFile())
        {
            // Empty slot — use it immediately
            oldestFile = candidate;
            allExist = false;
            break;
        }

        if (candidate.getLastModificationTime() <= oldestTime)
        {
            oldestTime = candidate.getLastModificationTime();
            oldestFile = candidate;
        }
    }

    if (oldestFile == juce::File{})
        return;  // Shouldn't happen

    // M13 (Loop-Aussetzer): te::EditFileOperations::saveAs is NOT playback-safe —
    // writeToFile() drops the audio threads' realtime priority for the duration
    // (EditPlaybackContext::RealtimePriorityDisabler) and edit.flushState() calls
    // suspendProcessing(true) on EVERY hosted plugin while its state is serialised,
    // so each plugin outputs silence for a moment. Result: the whole mix audibly
    // dropped out on every 2-minute autosave tick during playback.
    if (edit->getTransport().isPlaying())
    {
        // Playback-safe path: snapshot the state tree as-is. Everything that lives
        // in the ValueTree (clips, routing, volumes, pans, sends, automation) is
        // current; hosted-plugin state blobs are as fresh as the last full flush
        // (load / manual save / stopped autosave) — bounded staleness, zero audio
        // impact. createCopy() hands the background thread an exclusively-owned tree.
        writeAutosaveSnapshotAsync (edit->state.createCopy(), oldestFile);
        return;
    }

    // Transport stopped: full-fidelity autosave. The plugin suspends are inaudible.
    edit->flushState();
    te::EditFileOperations (*edit).saveAs (oldestFile, true);
}

void EditManager::writeAutosaveSnapshotAsync (juce::ValueTree exclusiveStateCopy,
                                              const juce::File& target)
{
    juce::Thread::launch ([tree = std::move (exclusiveStateCopy), target]
    {
        if (auto xml = tree.createXml())
        {
            // Temp-write + move: a crash mid-write must never leave a torn autosave
            // in the slot (the slot may still hold a good previous snapshot).
            const auto tmp = target.getSiblingFile (target.getFileName() + ".tmp");
            if (xml->writeTo (tmp) && ! tmp.moveFileTo (target))
                tmp.deleteFile();
        }
    });
}

juce::File EditManager::getLatestAutosave (const juce::File& sessionFile) const
{
    if (! sessionFile.existsAsFile())
        return {};

    const auto stem = sessionFile.getFileNameWithoutExtension();
    const auto dir  = sessionFile.getParentDirectory();

    juce::File latestFile;
    juce::Time latestTime;

    for (int i = 1; i <= kAutosaveKeptCount; ++i)
    {
        const auto candidate = dir.getChildFile (
            stem + "_autosave_" + juce::String (i) + ".tracktionedit");

        if (candidate.existsAsFile() &&
            candidate.getLastModificationTime() > latestTime)
        {
            latestTime  = candidate.getLastModificationTime();
            latestFile  = candidate;
        }
    }

    return latestFile;
}

//==============================================================================
void EditManager::moveClip (te::Clip& clip, double newStartTimeSecs)
{
    if (edit == nullptr)
    {
        jassertfalse;
        return;
    }

    // Each user moveClip begins a fresh transaction — creates one individual undo step.
    // This is the opposite of AI batch: 10 sequential moveClip() calls = 10 undo steps.
    edit->getUndoManager().beginNewTransaction ("Move clip");

    double lengthSecs = clip.getPosition().getLength().inSeconds();
    auto newStart = TimePos::fromSeconds (newStartTimeSecs);
    auto newEnd   = TimePos::fromSeconds (newStartTimeSecs + lengthSecs);
    clip.setPosition ({ TimeRng (newStart, newEnd), {} });
}

//==============================================================================
void EditManager::trimClip (te::Clip& clip, double newStartTimeSecs, double newEndTimeSecs)
{
    if (edit == nullptr)
    {
        jassertfalse;
        return;
    }

    // Each user trimClip begins a fresh transaction — creates one individual undo step.
    edit->getUndoManager().beginNewTransaction ("Trim clip");

    auto newStart = TimePos::fromSeconds (newStartTimeSecs);
    auto newEnd   = TimePos::fromSeconds (newEndTimeSecs);
    clip.setPosition ({ TimeRng (newStart, newEnd), {} });
}

//==============================================================================
// Autosave-Intervall (User-Wunsch 2026-08-15)
//==============================================================================

namespace
{
    const juce::Identifier kAutosaveMinutesKey ("aizanaAutosaveMinutes");
    constexpr int kAutosaveDefaultMinutes = 5;
}

int EditManager::getAutosaveIntervalMinutes() const
{
    auto& props = engine.getPropertyStorage().getPropertiesFile();
    const int stored = props.getIntValue (kAutosaveMinutesKey.toString(), kAutosaveDefaultMinutes);

    // Nur bekannte Stufen zulassen — ein aus Versehen editierter Wert soll die
    // App nicht in einen Sekundentakt schicken.
    for (int m : kAutosaveMinutesChoices)
        if (m == stored)
            return stored;

    return kAutosaveDefaultMinutes;
}

void EditManager::setAutosaveIntervalMinutes (int minutes)
{
    auto& props = engine.getPropertyStorage().getPropertiesFile();
    props.setValue (kAutosaveMinutesKey.toString(), minutes);
    props.saveIfNeeded();

    // Sofort wirksam: ein laufender Timer haette sonst noch bis zu zwei Minuten
    // im alten Takt weitergeschrieben.
    if (minutes > 0 && currentSessionFile.existsAsFile())
        autosaveTimer.startTimer (minutes * 60 * 1000);
    else
        autosaveTimer.stopTimer();
}
