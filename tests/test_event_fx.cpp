// test_event_fx.cpp — Phase 2+: Event-FX (plugins on the clip, not the track)

#include <catch2/catch_test_macros.hpp>

#include "engine/EditManager.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "plugins/BuiltInPluginFormat.h"

using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;

TEST_CASE ("PluginManager: insertPluginOnClip puts FX on the clip chain (undoable)", "[eventfx][plugins]")
{
    EditManager em;
    em.createNewEdit();
    PluginScanner scanner;
    PluginManager pm (scanner, em);

    auto* track = te::getAudioTracks (*em.getEdit())[0];
    te::ClipPosition pos { TimeRng (TimePos::fromSeconds (0.0), TimePos::fromSeconds (4.0)), {} };
    auto* clip = dynamic_cast<te::WaveAudioClip*> (
        track->insertNewClip (te::TrackItem::Type::wave, "clip", pos, nullptr));
    REQUIRE (clip != nullptr);
    REQUIRE (clip->getPluginList() != nullptr);

    // Insert a built-in FX as Event-FX
    juce::OwnedArray<juce::PluginDescription> builtIns;
    BuiltInPluginFormat::getBuiltInDescriptions (builtIns);
    REQUIRE (! builtIns.isEmpty());

    auto* fx = pm.insertPluginOnClip (*clip, *builtIns[0]);
    REQUIRE (fx != nullptr);

    // The plugin lives on the CLIP's chain, not the track's
    REQUIRE (clip->getPluginList()->getPlugins().contains (fx));
    REQUIRE_FALSE (track->pluginList.getPlugins().contains (fx));

    // Undo removes it again
    em.getUndoManager().undo();
    REQUIRE (clip->getPluginList()->getPlugins().isEmpty());

    em.getUndoManager().redo();
    REQUIRE (clip->getPluginList()->getPlugins().size() == 1);
}
