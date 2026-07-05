#include "LegacyMigration.h"

namespace aizana::legacy
{

// All "cloudes" literals below are the app's PRE-RENAME identity — intentional,
// never rename (see header).
namespace
{
    constexpr const char* kLegacyLower  = "cloudes";
    constexpr const char* kLegacyTitle  = "Cloudes";
    constexpr const char* kLegacyUpper  = "CLOUDES";
    constexpr const char* kNewLower     = "aizana";
    constexpr const char* kNewTitle     = "Aizana";
    constexpr const char* kNewUpper     = "AIZANA";

    juce::String uidHexFor (const juce::String& builtInIdentifier)
    {
        // Mirrors BuiltInPluginFormat::makeDesc: uniqueId = identifier.hashCode().
        return juce::String::toHexString (builtInIdentifier.hashCode());
    }
}

bool migrateDirectory (const juce::File& legacyDir, const juce::File& newDir)
{
    if (! legacyDir.isDirectory() || legacyDir == newDir)
        return false;

    newDir.createDirectory();
    bool movedAnything = false;

    for (const auto& child : legacyDir.findChildFiles (juce::File::findFilesAndDirectories, false))
    {
        const auto target = newDir.getChildFile (child.getFileName());

        // Existing targets win — a half-run migration must never destroy newer data.
        if (! target.exists())
            movedAnything = child.moveFileTo (target) || movedAnything;
    }

    if (legacyDir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        legacyDir.deleteFile();

    return movedAnything;
}

bool rewriteLegacyTokensInFile (const juce::File& xmlFile)
{
    if (! xmlFile.existsAsFile())
        return false;

    const auto text = xmlFile.loadFileAsString();
    if (! text.containsIgnoreCase (kLegacyLower))
        return false;

    return xmlFile.replaceWithText (text.replace (kLegacyLower, kNewLower)
                                        .replace (kLegacyTitle, kNewTitle)
                                        .replace (kLegacyUpper, kNewUpper));
}

void migrateNamesSidecar (const juce::File& generatedPluginsDir)
{
    const auto legacy  = generatedPluginsDir.getChildFile (juce::String (".") + kLegacyLower + "-names.json");
    if (! legacy.existsAsFile())
        return;

    const auto current = generatedPluginsDir.getChildFile (juce::String (".") + kNewLower + "-names.json");
    if (current.existsAsFile())
        legacy.deleteFile();          // both exist → the post-rename file is the live one
    else
        legacy.moveFileTo (current);
}

void migrateAppData()
{
    const auto library   = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    const auto legacyDir = library.getChildFile (kLegacyTitle);   // ~/Library/Cloudes
    const auto newDir    = library.getChildFile (kNewTitle);      // ~/Library/Aizana

    migrateDirectory (legacyDir, newDir);

    // Settings.xml + plugin cache: rename cloudes* keys/markers and repair cached
    // absolute paths that point into the pre-move directory. Unconditional — also
    // heals a partially completed earlier run. Only top-level XML: the generated
    // plugins sidecar (JSON) keys are real file names and must NOT be rewritten.
    for (const auto& xmlFile : newDir.findChildFiles (juce::File::findFiles, false, "*.xml"))
        rewriteLegacyTokensInFile (xmlFile);

    migrateNamesSidecar (newDir.getChildFile ("Generated Plugins"));
}

int migrateEditState (juce::ValueTree tree)
{
    int changes = 0;

    // 1) Property names: cloudes<X> → aizana<X> (bus numbers, channel types,
    //    VCA assignments, stereo flags, ...). Collect first — no mutation while iterating.
    juce::Array<juce::Identifier> legacyProps;
    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        const auto name = tree.getPropertyName (i);
        if (name.toString().startsWith (kLegacyLower))
            legacyProps.add (name);
    }

    for (const auto& oldName : legacyProps)
    {
        const juce::Identifier newName (kNewLower + oldName.toString().substring (7));
        if (! tree.hasProperty (newName))
            tree.setProperty (newName, tree[oldName], nullptr);
        tree.removeProperty (oldName, nullptr);
        ++changes;
    }

    // 2) PLUGIN identity. Built-in identifiers were "cloudes.<fx>" and TE matches
    //    primarily on uniqueId — which is identifier.hashCode() and therefore
    //    CHANGED with the rename. Rewrite identifier + recompute both uid fields,
    //    otherwise every old session loses its EQs/compressors without an error.
    if (tree.hasType (juce::Identifier ("PLUGIN")))
    {
        const auto fileOrId = tree.getProperty ("filename").toString();
        if (fileOrId.startsWith (juce::String (kLegacyLower) + "."))
        {
            const auto newId = juce::String (kNewLower) + fileOrId.substring (7);
            tree.setProperty ("filename", newId, nullptr);
            tree.setProperty ("uniqueId", uidHexFor (newId), nullptr);
            if (tree.hasProperty ("uid"))
                tree.setProperty ("uid", uidHexFor (newId), nullptr);
            ++changes;
        }

        const auto pluginName = tree.getProperty ("name").toString();
        if (pluginName.startsWith (juce::String (kLegacyTitle) + " "))
        {
            tree.setProperty ("name", juce::String (kNewTitle) + pluginName.substring (7), nullptr);
            ++changes;
        }

        if (tree.getProperty ("manufacturer").toString() == kLegacyTitle)
        {
            tree.setProperty ("manufacturer", kNewTitle, nullptr);
            ++changes;
        }
    }

    // 3) Stored paths into the migrated app-data dir (generated .clap refs etc.).
    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        const auto name  = tree.getPropertyName (i);
        const auto value = tree[name];
        if (value.isString())
        {
            const auto s = value.toString();
            if (s.contains (juce::String ("Library/") + kLegacyTitle))
            {
                tree.setProperty (name,
                                  s.replace (juce::String ("Library/") + kLegacyTitle,
                                             juce::String ("Library/") + kNewTitle),
                                  nullptr);
                ++changes;
            }
        }
    }

    for (auto child : tree)
        changes += migrateEditState (child);

    return changes;
}

bool migrateEditFile (const juce::File& editFile)
{
    if (! editFile.existsAsFile())
        return false;

    // Cheap pre-check: leave clean (post-rename) sessions completely untouched.
    if (! editFile.loadFileAsString().containsIgnoreCase (kLegacyLower))
        return false;

    const auto xml = juce::XmlDocument::parse (editFile);
    if (xml == nullptr)
        return false;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid() || migrateEditState (tree) == 0)
        return false;

    // replaceWithText writes via temp file + rename — the session is never half-written.
    return editFile.replaceWithText (tree.toXmlString());
}

juce::String envWithLegacyFallback (const char* newName, const char* legacyName,
                                    const juce::String& defaultValue)
{
    const juce::String unset ("\x01__unset__");

    if (auto v = juce::SystemStats::getEnvironmentVariable (newName, unset); v != unset)
        return v;

    if (auto v = juce::SystemStats::getEnvironmentVariable (legacyName, unset); v != unset)
        return v;

    return defaultValue;
}

} // namespace aizana::legacy
