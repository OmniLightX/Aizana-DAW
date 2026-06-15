#include "EditManager.h"
#include "plugins/BuiltInPluginFormat.h"
#include "plugins/ClapPluginFormat.h"

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
    // and the Cloudes-generated ones (Phase 4 / Iteration B). The scanner has its own
    // ClapPluginFormat; the engine needs its own to host them.
    engine.getPluginManager().pluginFormatManager.addFormat (new ClapPluginFormat());

    // Also add built-in descriptions to TE's knownPluginList so ExternalPlugin::findMatchingPlugin()
    // can locate them. TE's ExternalPlugin::doFullInitialisation() searches engine.getPluginManager().knownPluginList
    // — which is a separate object from PluginScanner::knownPlugins. Without this, getAudioPluginInstance()
    // returns null and FloatingPluginWindow shows "No editor available".
    juce::OwnedArray<juce::PluginDescription> builtIns;
    BuiltInPluginFormat::getBuiltInDescriptions (builtIns);
    for (auto* desc : builtIns)
        engine.getPluginManager().knownPluginList.addType (*desc);
}
EditManager::~EditManager() = default;

//==============================================================================
void EditManager::createNewEdit()
{
    // Create a fresh Edit with a single audio track.
    // The engine owns the Edit's UndoManager — all subsequent mutations route
    // through edit->getUndoManager() (the SINGLE shared undo manager per CONTEXT.md).
    edit = te::Edit::createSingleTrackEdit (engine);

    // Tracktion's WaveInputDevice records to "%projectdir%" and falls back to
    // juce::File::getCurrentWorkingDirectory() if an Edit has no file/project.
    // A macOS app launched from Finder can have an unwritable working directory,
    // causing recording contexts to fail before any WaveAudioClip is created.
    // Give unsaved sessions a stable writable media root immediately.
    auto unsavedDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                          .getChildFile ("Cloudes")
                          .getChildFile ("Unsaved Recordings");
    if (! unsavedDir.createDirectory())
        unsavedDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("Cloudes Unsaved Recordings");
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

    // Disable TE's built-in click track. Cloudes uses MetronomeGenerator instead —
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
void EditManager::loadEdit (const juce::File& file)
{
    // Use the Tracktion Engine helper that handles legacy format conversion.
    // loadEditFromFile() parses the XML, updates legacy state if needed, and
    // returns a fully constructed Edit ready for editing.
    edit = te::loadEditFromFile (engine, file, te::Edit::forEditing);

    if (edit == nullptr)
    {
        jassertfalse; // File missing, invalid, or engine failed to construct Edit
        return;
    }

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
        autosaveTimer.startTimer (kAutosaveIntervalMs);
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

    // Flush and write the autosave (overwrite the oldest slot)
    edit->flushState();
    te::EditFileOperations (*edit).saveAs (oldestFile, true);
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
