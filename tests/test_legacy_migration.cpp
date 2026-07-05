// test_legacy_migration.cpp — Rename-Migration Cloudes → Aizana (2026-07-05)
//
// The rename changed persisted identifiers (Klasse C of the rename sweep):
// cloudes* ValueTree properties, built-in plugin identifiers (whose uniqueId is
// identifier.hashCode() — i.e. the uid CHANGED), stored ~/Library/Cloudes paths,
// the .cloudes-names.json sidecar and the CLOUDES_OOP_PLUGINS env flag.
// These tests prove that pre-rename data survives:
//   "alte Session öffnet identisch" is the contract (rename-sweep Schritt 3).
//
// Every "cloudes" literal in this file is intentional legacy-fixture data.

#include <catch2/catch_test_macros.hpp>

#include "app/LegacyMigration.h"

#include <cstdlib>

namespace
{
    juce::File makeTempDir (const juce::String& name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_migration_tests")
                       .getChildFile (name + "_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        dir.createDirectory();
        return dir;
    }
}

// ---------------------------------------------------------------------------
// migrateEditState
// ---------------------------------------------------------------------------

TEST_CASE ("migrateEditState renames cloudes* properties recursively and keeps values", "[migration]")
{
    juce::ValueTree edit ("EDIT");
    juce::ValueTree track ("TRACK");
    track.setProperty ("cloudesTrackStereo", true, nullptr);
    track.setProperty ("cloudesBusNumber", 3, nullptr);
    track.setProperty ("cloudesChannelType", "bus", nullptr);
    track.setProperty ("name", "Drums", nullptr);          // user data — untouched
    edit.appendChild (track, nullptr);

    const int changes = aizana::legacy::migrateEditState (edit);

    REQUIRE (changes >= 3);
    CHECK (! track.hasProperty ("cloudesTrackStereo"));
    CHECK ((bool) track.getProperty ("aizanaTrackStereo") == true);
    CHECK ((int) track.getProperty ("aizanaBusNumber") == 3);
    CHECK (track.getProperty ("aizanaChannelType").toString() == "bus");
    CHECK (track.getProperty ("name").toString() == "Drums");

    // Second run is a no-op (idempotent).
    CHECK (aizana::legacy::migrateEditState (edit) == 0);
}

TEST_CASE ("migrateEditState rewrites built-in plugin identity INCLUDING the uid hash", "[migration]")
{
    // Pre-rename session: built-in EQ stored with identifier "cloudes.eq" and
    // uniqueId = "cloudes.eq".hashCode() (see BuiltInPluginFormat::makeDesc).
    juce::ValueTree plugin ("PLUGIN");
    plugin.setProperty ("type", "external", nullptr);
    plugin.setProperty ("filename", "cloudes.eq", nullptr);
    plugin.setProperty ("name", "Cloudes EQ", nullptr);
    plugin.setProperty ("manufacturer", "Cloudes", nullptr);
    plugin.setProperty ("uniqueId", juce::String::toHexString (juce::String ("cloudes.eq").hashCode()), nullptr);
    plugin.setProperty ("uid",      juce::String::toHexString (juce::String ("cloudes.eq").hashCode()), nullptr);

    juce::ValueTree edit ("EDIT");
    edit.appendChild (plugin, nullptr);

    REQUIRE (aizana::legacy::migrateEditState (edit) > 0);

    const auto expectedUid = juce::String::toHexString (juce::String ("aizana.eq").hashCode());
    CHECK (plugin.getProperty ("filename").toString() == "aizana.eq");
    CHECK (plugin.getProperty ("name").toString() == "Aizana EQ");
    CHECK (plugin.getProperty ("manufacturer").toString() == "Aizana");
    CHECK (plugin.getProperty ("uniqueId").toString() == expectedUid);
    CHECK (plugin.getProperty ("uid").toString() == expectedUid);

    // The whole point: the hash really did change with the rename.
    CHECK (juce::String ("aizana.eq").hashCode() != juce::String ("cloudes.eq").hashCode());
}

TEST_CASE ("migrateEditState rewrites stored ~/Library/Cloudes paths", "[migration]")
{
    juce::ValueTree plugin ("PLUGIN");
    plugin.setProperty ("filename", "/Users/x/Library/Cloudes/Generated Plugins/MegaVerb.clap", nullptr);

    REQUIRE (aizana::legacy::migrateEditState (plugin) > 0);
    CHECK (plugin.getProperty ("filename").toString()
               == "/Users/x/Library/Aizana/Generated Plugins/MegaVerb.clap");
}

TEST_CASE ("migrateEditState leaves user track names containing the old word alone", "[migration]")
{
    // Only PLUGIN identity is rewritten — a TRACK the user happened to call
    // "Cloudes Demo" is user data, not app identity.
    juce::ValueTree track ("TRACK");
    track.setProperty ("name", "Cloudes Demo", nullptr);

    CHECK (aizana::legacy::migrateEditState (track) == 0);
    CHECK (track.getProperty ("name").toString() == "Cloudes Demo");
}

// ---------------------------------------------------------------------------
// migrateEditFile
// ---------------------------------------------------------------------------

TEST_CASE ("migrateEditFile upgrades a pre-rename session file in place", "[migration]")
{
    const auto dir  = makeTempDir ("editfile");
    const auto file = dir.getChildFile ("old-session.tracktionedit");

    juce::ValueTree edit ("EDIT");
    juce::ValueTree track ("TRACK");
    track.setProperty ("cloudesVCA", 2, nullptr);
    juce::ValueTree plugin ("PLUGIN");
    plugin.setProperty ("filename", "cloudes.compressor", nullptr);
    plugin.setProperty ("uniqueId", juce::String::toHexString (juce::String ("cloudes.compressor").hashCode()), nullptr);
    track.appendChild (plugin, nullptr);
    edit.appendChild (track, nullptr);
    REQUIRE (file.replaceWithText (edit.toXmlString()));

    REQUIRE (aizana::legacy::migrateEditFile (file));

    const auto xml = juce::XmlDocument::parse (file);
    REQUIRE (xml != nullptr);
    const auto reloaded = juce::ValueTree::fromXml (*xml);
    const auto reTrack  = reloaded.getChild (0);
    const auto rePlugin = reTrack.getChild (0);

    CHECK ((int) reTrack.getProperty ("aizanaVCA") == 2);
    CHECK (rePlugin.getProperty ("filename").toString() == "aizana.compressor");
    CHECK (rePlugin.getProperty ("uniqueId").toString()
               == juce::String::toHexString (juce::String ("aizana.compressor").hashCode()));

    // Clean file → second call is a no-op.
    CHECK (! aizana::legacy::migrateEditFile (file));

    dir.deleteRecursively();
}

// ---------------------------------------------------------------------------
// Directory / sidecar / settings migration
// ---------------------------------------------------------------------------

TEST_CASE ("migrateDirectory merge-moves and never overwrites newer data", "[migration]")
{
    const auto root   = makeTempDir ("dirmove");
    const auto legacy = root.getChildFile ("Cloudes");
    const auto fresh  = root.getChildFile ("Aizana");
    legacy.createDirectory();

    legacy.getChildFile ("Settings.xml").replaceWithText ("<PROPERTIES/>");
    legacy.getChildFile ("Generated Plugins").createDirectory();
    legacy.getChildFile ("Generated Plugins").getChildFile ("Verb.clap").replaceWithText ("fake");

    // Target partially exists — the existing file must WIN.
    fresh.createDirectory();
    fresh.getChildFile ("keepme.txt").replaceWithText ("new data");
    legacy.getChildFile ("keepme.txt").replaceWithText ("old data");

    REQUIRE (aizana::legacy::migrateDirectory (legacy, fresh));

    CHECK (fresh.getChildFile ("Settings.xml").existsAsFile());
    CHECK (fresh.getChildFile ("Generated Plugins").getChildFile ("Verb.clap").existsAsFile());
    CHECK (fresh.getChildFile ("keepme.txt").loadFileAsString() == "new data");
    CHECK (legacy.getChildFile ("keepme.txt").existsAsFile());   // loser stays in place, not destroyed
    CHECK (! legacy.getChildFile ("Settings.xml").exists());

    root.deleteRecursively();
}

TEST_CASE ("rewriteLegacyTokensInFile renames keys and cached paths inside settings XML", "[migration]")
{
    const auto dir  = makeTempDir ("tokens");
    const auto file = dir.getChildFile ("Settings.xml");
    file.replaceWithText ("<PROPERTIES><VALUE name=\"cloudes_ui_mode\" val=\"pro\"/>"
                          "<VALUE name=\"cache\" val=\"/Users/x/Library/Cloudes/plugins.xml\"/>"
                          "<PLUGINS cloudesScanComplete=\"1\"/></PROPERTIES>");

    REQUIRE (aizana::legacy::rewriteLegacyTokensInFile (file));

    const auto text = file.loadFileAsString();
    CHECK (text.contains ("aizana_ui_mode"));
    CHECK (text.contains ("/Users/x/Library/Aizana/plugins.xml"));
    CHECK (text.contains ("aizanaScanComplete"));
    CHECK (! text.containsIgnoreCase ("cloudes"));

    // Clean file → no rewrite reported.
    CHECK (! aizana::legacy::rewriteLegacyTokensInFile (file));

    dir.deleteRecursively();
}

TEST_CASE ("migrateNamesSidecar carries the display-name override file over", "[migration]")
{
    const auto dir = makeTempDir ("sidecar");

    SECTION ("legacy only → moved")
    {
        dir.getChildFile (".cloudes-names.json").replaceWithText ("{\"Verb.clap\":\"My Verb\"}");
        aizana::legacy::migrateNamesSidecar (dir);
        CHECK (! dir.getChildFile (".cloudes-names.json").exists());
        CHECK (dir.getChildFile (".aizana-names.json").loadFileAsString().contains ("My Verb"));
    }

    SECTION ("both exist → post-rename file wins, stale legacy removed")
    {
        dir.getChildFile (".cloudes-names.json").replaceWithText ("{\"a\":\"old\"}");
        dir.getChildFile (".aizana-names.json").replaceWithText ("{\"a\":\"new\"}");
        aizana::legacy::migrateNamesSidecar (dir);
        CHECK (! dir.getChildFile (".cloudes-names.json").exists());
        CHECK (dir.getChildFile (".aizana-names.json").loadFileAsString().contains ("new"));
    }

    dir.deleteRecursively();
}

// ---------------------------------------------------------------------------
// Env-flag fallback
// ---------------------------------------------------------------------------

TEST_CASE ("envWithLegacyFallback honours the pre-rename OOP flag", "[migration]")
{
    ::unsetenv ("AIZANA_TEST_MIG_NEW");
    ::unsetenv ("AIZANA_TEST_MIG_OLD");

    CHECK (aizana::legacy::envWithLegacyFallback ("AIZANA_TEST_MIG_NEW", "AIZANA_TEST_MIG_OLD", "0") == "0");

    ::setenv ("AIZANA_TEST_MIG_OLD", "1", 1);
    CHECK (aizana::legacy::envWithLegacyFallback ("AIZANA_TEST_MIG_NEW", "AIZANA_TEST_MIG_OLD", "0") == "1");

    ::setenv ("AIZANA_TEST_MIG_NEW", "0", 1);   // new flag set → wins over legacy
    CHECK (aizana::legacy::envWithLegacyFallback ("AIZANA_TEST_MIG_NEW", "AIZANA_TEST_MIG_OLD", "1") == "0");

    ::unsetenv ("AIZANA_TEST_MIG_NEW");
    ::unsetenv ("AIZANA_TEST_MIG_OLD");
}
