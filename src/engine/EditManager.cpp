#include "EditManager.h"
#include "WarpEngine.h"
#include "app/LegacyMigration.h"
#include "plugins/BuiltInPluginFormat.h"
#include "plugins/ClapPluginFormat.h"
#include "RemovableMediaGuard.h"

// Convenience aliases for Tracktion's strong time types
using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;

namespace
{
    juce::File crashMarkerFile();

    void visitFileSources (juce::ValueTree tree,
                          const std::function<void (juce::ValueTree, const juce::String&)>& visit)
    {
        if ((tree.hasType (te::IDs::AUDIOCLIP) || tree.hasType (te::IDs::TAKE)
             || tree.hasType (te::IDs::EDITCLIP)) && tree.hasProperty (te::IDs::source))
        {
            const auto source = tree[te::IDs::source].toString();
            if (source.isNotEmpty() && ! te::ProjectItemID (source).isValid())
                visit (tree, source);
        }
        for (auto child : tree)
            visitFileSources (child, visit);
    }
}

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
    savedSourceAliases.clear();
    originalSourceResolver = {};
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
    hasChosenSaveDestination = false;
    unsavedDir.setAsCurrentWorkingDirectory();

    // The session file must EXIST on disk even before the first save:
    // TE stores relative source refs (e.g. loop-take files) via
    // File::getRelativePathFrom (editFileRetriever()) — and JUCE only treats
    // the base as a file when it exists, otherwise as a directory, which bakes
    // one extra "../" into every stored take path (= unresolvable sources).
    if (! currentSessionFile.existsAsFile())
        currentSessionFile.create();

    applyRecordingLocation(); // The placeholder must exist before deriving the media path.

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

    // TEs Klickspur ist seit 2026-08-16 UNSER Metronom (siehe src/engine/CountIn.h).
    // Hier stand vorher `edit->clickTrackEnabled = false;`, weil Aizana daneben einen
    // eigenen, frei laufenden Klickgenerator hatte und zwei Quellen kollidiert
    // waeren. Der eigene Generator ist weg — diese Zeile wuerde jetzt bei JEDEM
    // Laden das Metronom des Anwenders stumm schalten. Der Zustand kommt aus der
    // Session; die Transportleiste liest ihn beim Anhaengen aus.
    //
    // Der Vorzaehler-Modus dagegen ist eine GLOBALE Engine-Einstellung und wird nur
    // fuer die Dauer eines Aufnahmestarts geliehen — im Ruhezustand gehoert er aus,
    // sonst wuerde TE bei jedem Aufnehmen zusaetzlich vorzaehlen.
    edit->setCountInMode (te::Edit::CountIn::none);

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
bool EditManager::loadEdit (const juce::File& file, juce::File adoptAsSession)
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

    savedSourceAliases.clear();
    originalSourceResolver = {};
    edit = std::move (newEdit);
    warpRenderWatcher.attach (*edit);

    // Same as createNewEdit: allocate context so input devices are immediately accessible.
    edit->getTransport().ensureContextAllocated();

    // TE's default filePathResolver (sibling-based) lacks the legacy fallback for
    // take refs recorded in unsaved sessions (see createNewEdit) — install ours.
    // Beim Wiederherstellen wird eine Datei GELESEN, aber eine andere ist die
    // Session: der Schnappschuss liegt in `Backups/`, und relative
    // Dateiverweise (Takes!) muessen weiter gegen die SESSION aufgeloest werden —
    // sonst zeigt jeder Verweis eine Ebene daneben (I-07).
    const auto editFile = adoptAsSession != juce::File() ? adoptAsSession : file;
    setCurrentFile (editFile);
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
juce::Result EditManager::saveEdit (const juce::File& file)
{
    if (edit == nullptr)
        return juce::Result::fail ("Keine Session geladen.");
    const auto previousSession = currentSessionFile;

    if (edit->getTransport().isRecording() && (needsSaveAs() || file != currentSessionFile))
    {
        // Copying a still-growing WAV would publish a truncated take and pin
        // the resolver to that incomplete copy. Finalise the take first.
        return juce::Result::fail ("Bitte die Aufnahme beenden und danach erneut speichern.");
    }
    if (! file.getParentDirectory().createDirectory())
    {
        return juce::Result::fail (file.getParentDirectory().getFullPathName());
    }

    // Let TE serialize plugin/control state, but never publish an XML file whose
    // media have not been collected yet. The staging file is a sibling, so the
    // final replacement is on the same volume and keeps an existing save intact.
    juce::TemporaryFile pending (file);
    te::CustomControlSurface::saveAllSettings (engine);
    edit->getParameterControlMappings().saveToEdit();
    if (! te::EditFileOperations (*edit).writeToFile (pending.getFile(), false))
    {
        return juce::Result::fail (file.getFullPathName());
    }
    auto xml = juce::XmlDocument::parse (pending.getFile());
    if (xml == nullptr)
    {
        return juce::Result::fail ("Session-Schnappschuss ist ungueltig.");
    }
    auto snapshot = juce::ValueTree::fromXml (*xml);
    auto aliases = savedSourceAliases;
    const auto media = aizana::sessionpaths::mediaFolder (file);
    const auto oldMedia = mediaDirectory();
    std::map<juce::String, juce::File> collected;
    juce::String error;

    const auto collect = [&] (const juce::File& original) -> juce::File
    {
        if (error.isNotEmpty())
            return {};
        if (! original.existsAsFile())
        {
            error = "Audiodatei fehlt: " + original.getFullPathName();
            return {};
        }
        auto found = collected.find (original.getFullPathName());
        juce::File target;
        if (found != collected.end())
            target = found->second;
        else
        {
            if (original.isAChildOf (media))
                target = original;
            else
            {
                if (edit->getTransport().isRecording())
                {
                    error = "Bitte die Aufnahme vor dem Einsammeln externer Audiodateien beenden.";
                    return {};
                }
                // Retain Comps/Bounces and other owned subdirectories. External
                // imports are collected beside recordings with collision-safe names.
                target = original.isAChildOf (oldMedia)
                    ? media.getChildFile (original.getRelativePathFrom (oldMedia))
                    : media.getChildFile (original.getFileName());
                if (target.existsAsFile() && ! target.hasIdenticalContentTo (original))
                    target = target.getNonexistentSibling();
                if (! target.existsAsFile())
                {
                    if (! target.getParentDirectory().createDirectory())
                    {
                        error = "Medienordner nicht schreibbar: " + target.getParentDirectory().getFullPathName();
                        return {};
                    }
                    juce::TemporaryFile copy (target);
                    if (! original.copyFileTo (copy.getFile()) || ! copy.overwriteTargetFileWithTemporary())
                    {
                        error = "Audiodatei konnte nicht kopiert werden: " + original.getFullPathName();
                        return {};
                    }
                }
            }
            collected.emplace (original.getFullPathName(), target);
        }
        return target;
    };

    // Retain owned media even when currently present only in undo history.
    // Preserving occupied recording filenames also prevents a new take from
    // reusing an old alias after Save As with the same session basename.
    if (oldMedia != media && oldMedia.isDirectory())
        for (const auto& owned : oldMedia.findChildFiles (juce::File::findFiles, true))
        {
            if (file.getParentDirectory().isAChildOf (oldMedia)
                && owned.isAChildOf (file.getParentDirectory()))
                continue; // A destination inside the old media tree must not collect itself.
            const auto target = collect (owned);
            if (error.isNotEmpty())
                break;
            aliases.emplace (owned.getFullPathName(), target);
            aliases.emplace (owned.getRelativePathFrom (currentSessionFile.getParentDirectory()), target);
        }
    for (auto& entry : aliases)
        if (auto it = collected.find (entry.second.getFullPathName()); it != collected.end())
            entry.second = it->second;

    visitFileSources (snapshot, [&] (juce::ValueTree node, const juce::String& source)
    {
        const auto target = collect (te::SourceFileReference::findFileFromString (*edit, source));
        if (error.isNotEmpty())
            return;
        // Detached tree only: editing state and undo actions are not mutated.
        node.setProperty (te::IDs::source, target.getRelativePathFrom (file.getParentDirectory()), nullptr);
        aliases[source] = target;
    });
    if (error.isNotEmpty())
    {
        return juce::Result::fail (error);
    }
    xml = snapshot.createXml();
    if (xml == nullptr || ! xml->writeTo (pending.getFile()) || ! pending.overwriteTargetFileWithTemporary())
    {
        return juce::Result::fail (file.getFullPathName());
    }

    // Commit the new identity only after publication. Immutable captures keep
    // resolver calls independent of subsequent map mutations and manager lifetime.
    if (! originalSourceResolver)
        originalSourceResolver = edit->filePathResolver;
    savedSourceAliases = std::move (aliases);
    edit->editFileRetriever = [file] { return file; };
    edit->filePathResolver = [file, map = savedSourceAliases, fallback = originalSourceResolver]
                            (const juce::String& source) -> juce::File
    {
        if (auto it = map.find (source); it != map.end())
            return it->second;
        if (juce::File::isAbsolutePath (source))
            return juce::File (source);
        auto local = file.getParentDirectory().getChildFile (source);
        if (local.existsAsFile())
            return local;
        return fallback ? fallback (source) : local;
    };
    setCurrentFile (file);
    hasChosenSaveDestination = true;
    edit->resetChangedStatus();
    engine.getEngineBehaviour().editHasBeenSaved (*edit, file);
    edit->sendSourceFileUpdate();
    // A successful explicit save supersedes this session's recovery marker,
    // including when both files happen to have the same filesystem timestamp.
    const auto marker = crashMarkerFile();
    const auto markerSession = juce::JSON::parse (marker.loadFileAsString())
                                   .getProperty ("session", {}).toString();
    if (markerSession == previousSession.getFullPathName() || markerSession == file.getFullPathName())
        marker.deleteFile();
    return juce::Result::ok();
}

juce::ValueTree EditManager::snapshotWithResolvedSources() const
{
    auto snapshot = edit->state.createCopy();
    // Recovery/backup files live in Backups, but are adopted as the session on
    // restore. Normalize against that session, never the backup's own directory.
    visitFileSources (snapshot, [&] (juce::ValueTree node, const juce::String& source)
    {
        const auto resolved = te::SourceFileReference::findFileFromString (*edit, source);
        if (resolved == juce::File())
            return;
        const auto root = currentSessionFile.getParentDirectory();
        node.setProperty (te::IDs::source,
            resolved.isAChildOf (root) ? resolved.getRelativePathFrom (root) : resolved.getFullPathName(), nullptr);
    });
    return snapshot;
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

juce::File EditManager::mediaDirectory() const
{
    // ALLES, was zu einer Session gehoert, liegt in EINEM Ordner neben ihr:
    //   <Session-Ordner>/<Sessionname> Audio/
    //
    // ANLASS (User-Befund 2026-08-18): Aufnahmen landeten bis dahin LOSE im
    // Ordner der Session — bei ihm also direkt im Wurzelverzeichnis seiner SSD,
    // zwischen Logic- und Studio-One-Ordnern, und mit Namen wie
    // „_Track 8_Take_1.wav" (der fuehrende Unterstrich, weil der Edit gar keinen
    // Namen hatte). Er hat sie aufgeraeumt — voellig zu Recht, nichts daran sah
    // nach Projektinhalt aus — und damit waren seine Takes weg: der
    // Comping-Editor meldete nur noch „Quelldatei nicht gefunden".
    //
    // TE bestimmt das Aufnahmeziel ueber `WaveInputDevice::filenameMask`; ohne
    // Project-Verwaltung (I-07: Aizana nutzt direkte Dateiverweise) faellt
    // `%projectdir%` auf das Verzeichnis der Edit-Datei zurueck. Genau dieser
    // Fallback war die Ursache.
    //
    // Ohne gespeicherte Session gibt es keinen Sessionordner — dann in die
    // Dokumente, aber ebenfalls in einen benannten Ordner, nie irgendwohin.
    if (currentSessionFile.existsAsFile())
        return aizana::sessionpaths::mediaFolder (currentSessionFile);

    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
             .getChildFile ("Aizana").getChildFile ("Unbenannte Session");
}

juce::String EditManager::recordingFilenameMask() const
{
    // Dateiname: <Session>_<Spur>_Take_<n>.
    //
    // TEs Platzhalter %edit% ist hier NICHT brauchbar: `Edit::getName()` liest den
    // Namen aus dem ProjectItem, und ohne Project-Verwaltung (I-07) gibt es keines
    // — der Name ist immer leer. Genau daher der fuehrende Unterstrich in den
    // alten Aufnahmen („_Track 8_Take_1.wav"): eine Datei, der man nicht ansieht,
    // wozu sie gehoert. Also setzen wir den Sessionnamen direkt ein.
    const auto sessionName = currentSessionFile.existsAsFile()
                               ? aizana::sessionpaths::sessionStemFrom (currentSessionFile)
                               : juce::String ("Unbenannt");

    return mediaDirectory().getFullPathName() + juce::File::getSeparatorString()
             + sessionName + "_%track%_Take_%take%";
}

void EditManager::applyRecordingLocation()
{
    mediaDirectory().createDirectory();

    const auto mask = recordingFilenameMask();
    auto& dm = engine.getDeviceManager();

    // Flush pending device rebuilds: new inputs restore their old per-device mask.
    for (auto* device : dm.getWaveInputDevices())
        device->setFilenameMask (mask);
}

void EditManager::setCurrentFile (const juce::File& file)
{
    currentSessionFile = file;
    // Recovery can adopt the internal recording placeholder too. Compare full
    // reserved paths, not the name: a user's song named Untitled is still named.
    const auto placeholder = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
        .getChildFile ("Aizana/Unsaved Recordings/Untitled.tracktionedit");
    const auto fallback = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("Aizana Unsaved Recordings/Untitled.tracktionedit");
    hasChosenSaveDestination = file != juce::File() && file != placeholder && file != fallback;
    applyRecordingLocation();

    if (file.existsAsFile())
    {
        // Start autosave timer on a valid session file
        // 0 Minuten = aus; sonst der eingestellte Abstand.
        if (const int mins = getAutosaveIntervalMinutes(); mins > 0)
            autosaveTimer.startTimer (mins * 60 * 1000);
        else
            autosaveTimer.stopTimer();

        // Das Netz gegen den Absturz laeuft UNABHAENGIG vom Autosave-Abstand —
        // auch dann, wenn der Anwender den Autosave ganz abgeschaltet hat.
        recoveryTimer.startTimer (kRecoverySeconds * 1000);
    }
    else
    {
        autosaveTimer.stopTimer();
        recoveryTimer.stopTimer();
    }
}

void EditManager::performAutosave()
{
    if (edit == nullptr || ! currentSessionFile.existsAsFile())
        return;

    // Die Sicherungen liegen im UNTERORDNER der Session, nicht daneben.
    //
    // Vorher hiessen sie `<Name>_autosave_N.tracktionedit` und lagen gleichwertig
    // neben der Session: neuer als sie, in jedem Dateidialog obenauf. Ein Anwender
    // hat prompt eine geoeffnet und darin weitergearbeitet — auf seiner Platte
    // stand danach `<Name>_autosave_1_autosave_2`. Ein Sicherungsstand darf nicht
    // aussehen wie das Projekt.
    const auto dir = aizana::sessionpaths::backupFolder (currentSessionFile);
    dir.createDirectory();

    // Find the next slot (rotate among kAutosaveKeptCount slots)
    // The slot with the oldest modification time gets overwritten.
    juce::File oldestFile;
    juce::Time oldestTime = juce::Time::getCurrentTime();
    bool allExist = true;

    for (int i = 1; i <= kAutosaveKeptCount; ++i)
    {
        const auto candidate = dir.getChildFile (
            aizana::sessionpaths::backupFileName (currentSessionFile, i));

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
        writeAutosaveSnapshotAsync (snapshotWithResolvedSources(), oldestFile);
        return;
    }

    // Transport stopped: full-fidelity autosave. The plugin suspends are inaudible.
    edit->flushState();
    const auto snapshot = snapshotWithResolvedSources();
    if (auto xml = snapshot.createXml())
    {
        juce::TemporaryFile pending (oldestFile);
        if (xml->writeTo (pending.getFile()))
            pending.overwriteTargetFileWithTemporary();
    }
}

void EditManager::writeAutosaveSnapshotAsync (juce::ValueTree exclusiveStateCopy,
                                              const juce::File& target)
{
    // Auf dem EIGENEN Pool, nicht als losgeloester Thread.
    //
    // `juce::Thread::launch` startet einen Thread, den niemand mehr einholt: er
    // schreibt weiter, waehrend der EditManager (und mit ihm die Engine) schon
    // zerlegt wird. Solange das nur alle paar Minuten passierte, fiel es nicht
    // auf — mit dem Wiederherstellungs-Schnappschuss im Minutentakt schon:
    // im vollen Testlauf 2026-08-18 ein SIGSEGV, das beim Einzellauf desselben
    // Tests nie auftrat (ein klassisches Zeichen fuer „Leben ueber die eigene
    // Lebenszeit hinaus"). Der Pool gehoert dem EditManager; sein Destruktor
    // wartet auf laufende Jobs, bevor irgendetwas anderes zerlegt wird.
    snapshotPool.addJob ([tree = std::move (exclusiveStateCopy), target]
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

//==============================================================================
// Wiederherstellung nach Absturz
//
// Der Anwender hat die Luecke benannt: „Was ist, wenn das Programm einmal
// abstuerzt? Autosave bei 60 Minuten, Absturz bei 59 — dann sind 59 Minuten
// Arbeit einfach weg." Genau dafuer ist der rotierende Autosave nicht gedacht;
// er ist die Zeitmaschine, nicht das Netz.
//
// Also ein zweiter, unabhaengiger Weg, so wie ihn Studio One und Logic fahren:
//   * ein billiger Schnappschuss des ValueTree im MINUTENTAKT, immer in DIESELBE
//     Datei (`Backups/<Name>_Wiederherstellung.tracktionedit`),
//   * eine MARKE in den App-Daten, die sagt „hier ist eine Sitzung offen",
//   * beim sauberen Beenden wird die Marke geloescht.
// Existiert die Marke beim naechsten Start, ist die letzte Sitzung NICHT sauber
// beendet worden — und nur dann wird gefragt.
//
// Der Schnappschuss laeuft ueber denselben playback-sicheren Weg wie der
// Autosave waehrend der Wiedergabe (ValueTree-Kopie, Schreiben im Hintergrund):
// er darf NIE ein Dropout verursachen, sonst schaltet ihn jeder wieder ab.

namespace
{
    juce::File crashMarkerFile()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("Aizana");
        dir.createDirectory();
        return dir.getChildFile ("offene-sitzung.json");
    }
}

std::optional<EditManager::CrashRecovery> EditManager::findCrashRecovery()
{
    const auto marker = crashMarkerFile();

    if (! marker.existsAsFile())
        return {};                       // sauber beendet — nichts zu tun

    auto json = juce::JSON::parse (marker.loadFileAsString());
    const juce::File session  (json.getProperty ("session",  {}).toString());
    const juce::File snapshot (json.getProperty ("snapshot", {}).toString());

    if (! snapshot.existsAsFile())
    {
        marker.deleteFile();             // nichts zu holen — Marke weg
        return {};
    }

    // Ist der Schnappschuss ueberhaupt juenger als die Session? Wer nach dem
    // letzten Schnappschuss noch gespeichert hat, verliert nichts — dann waere
    // die Frage nur Laerm.
    if (session.existsAsFile()
        && (snapshot.getLastModificationTime() < session.getLastModificationTime()
            || (snapshot.getLastModificationTime() == session.getLastModificationTime()
                && snapshot.hasIdenticalContentTo (session))))
    {
        marker.deleteFile();
        return {};
    }

    CrashRecovery r;
    r.session  = session;
    r.snapshot = snapshot;
    r.when     = snapshot.getLastModificationTime();
    return r;
}

void EditManager::clearCrashMarker()
{
    crashMarkerFile().deleteFile();
}

void EditManager::writeRecoverySnapshot()
{
    if (edit == nullptr || ! currentSessionFile.existsAsFile())
        return;

    const auto target = aizana::sessionpaths::recoveryFile (currentSessionFile);
    target.getParentDirectory().createDirectory();

    // Bei STEHENDEM Transport zusaetzlich die Plugin-Staende einsammeln — das
    // suspendiert kurz jedes Plugin und ist nur deshalb vertretbar, weil nichts
    // laeuft. Waehrend der Wiedergabe NICHT (M13: genau das verursachte die
    // Aussetzer beim Autosave-Tick).
    if (! edit->getTransport().isPlaying())
        edit->flushState();

    writeAutosaveSnapshotAsync (snapshotWithResolvedSources(), target);

    // Marke aktualisieren: WELCHE Session, WELCHER Schnappschuss. Sie ist der
    // einzige Unterschied zwischen „abgestuerzt" und „sauber beendet".
    juce::DynamicObject::Ptr o (new juce::DynamicObject());
    o->setProperty ("session",  currentSessionFile.getFullPathName());
    o->setProperty ("snapshot", target.getFullPathName());
    o->setProperty ("zeit",     juce::Time::getCurrentTime().toISO8601 (true));
    crashMarkerFile().replaceWithText (juce::JSON::toString (o.get()));
}

juce::File EditManager::getLatestAutosave (const juce::File& sessionFile) const
{
    if (! sessionFile.existsAsFile())
        return {};

    const auto dir = aizana::sessionpaths::backupFolder (sessionFile);

    juce::File latestFile;
    juce::Time latestTime;

    for (int i = 1; i <= kAutosaveKeptCount; ++i)
    {
        const auto candidate = dir.getChildFile (
            aizana::sessionpaths::backupFileName (sessionFile, i));

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
