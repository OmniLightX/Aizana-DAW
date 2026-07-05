#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace aizana::legacy
{
    /*  Rename-Migration Cloudes → Aizana (2026-07-05).

        Every "cloudes"/"Cloudes" literal in this module is INTENTIONAL: it names
        data written by the app's former identity so it keeps working after the
        rename. These literals are Klasse C of the rename sweep (aiz-release
        references/rename-sweep.md) and must never be "renamed along".

        Covered legacy surfaces:
          - ~/Library/Cloudes            → merged into ~/Library/Aizana (settings,
                                           plugin cache, generated plugin library,
                                           ML models, training corpus)
          - Settings.xml / plugin cache  → "cloudes*" keys + stored ".../Library/Cloudes/..."
                                           paths rewritten in place
          - .tracktionedit sessions      → cloudes* ValueTree properties, built-in
                                           plugin identity (identifier + uniqueId hash!),
                                           stored library paths
          - .cloudes-names.json sidecar  → renamed to .aizana-names.json
          - CLOUDES_OOP_PLUGINS          → read as fallback for AIZANA_OOP_PLUGINS

        NOT migrated on purpose:
          - ~/Music/Cloudes (unsaved recordings): old sessions reference those media
            files by absolute path — moving them would silently break playback.
            New sessions record to ~/Music/Aizana; the old folder stays valid.
    */

    /** One-time app-data migration. Call at startup BEFORE the te::Engine is
        constructed (the engine creates/opens ~/Library/Aizana on construction).
        Idempotent and cheap when there is nothing to migrate. */
    void migrateAppData();

    /** Merge-moves legacyDir's children into newDir (existing targets win — never
        destroys data), removes legacyDir once drained. Returns true if anything moved. */
    bool migrateDirectory (const juce::File& legacyDir, const juce::File& newDir);

    /** Rewrites cloudes/Cloudes/CLOUDES tokens inside a settings/cache XML file
        (property keys, scan markers, cached absolute paths). No-op when clean. */
    bool rewriteLegacyTokensInFile (const juce::File& xmlFile);

    /** Renames the generated-plugin display-name sidecar .cloudes-names.json →
        .aizana-names.json. Keys/values are left untouched (keys are .clap file
        names on disk, values are user-chosen display names). */
    void migrateNamesSidecar (const juce::File& generatedPluginsDir);

    /** Recursively migrates a loaded edit/session tree:
          - property names "cloudes<X>" → "aizana<X>"
          - PLUGIN nodes: built-in identifiers "cloudes.<fx>" → "aizana.<fx>" with
            RECOMPUTED uniqueId/uid (the uid is identifier.hashCode() — it changed
            with the rename; without this, old sessions silently lose all built-in FX)
          - PLUGIN name "Cloudes <X>" → "Aizana <X>", manufacturer "Cloudes" → "Aizana"
          - string values containing "Library/Cloudes" → "Library/Aizana"
        Returns the number of changes. Uses no UndoManager (silent normalisation). */
    int migrateEditState (juce::ValueTree tree);

    /** Parses a .tracktionedit file, runs migrateEditState, and atomically writes
        the migrated XML back when anything changed. Safe to call on every load. */
    bool migrateEditFile (const juce::File& editFile);

    /** Env-var read that prefers newName but still honours the pre-rename variable. */
    juce::String envWithLegacyFallback (const char* newName, const char* legacyName,
                                        const juce::String& defaultValue);
}
