// test_plugin_param_automation.cpp — T1.1: Automation beliebiger Plugin-Parameter.
//
// Deckt: qualifizierte plug:-IDs (eindeutig bei ZWEI Instanzen desselben Plugins),
// die UI-Parameterliste (Volume/Pan + Plugin-Params, interne Plugins gefiltert),
// Punkte auf einem Plugin-Parameter (nativ, undobar) und die Norm-Helfer
// (Pan -1..1 — der alte 0..1-Pfad konnte nur Mitte→Rechts).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/EditManager.h"
#include "engine/AutomationEngine.h"

namespace
{
    te::Plugin::Ptr insertTePlugin (te::Edit& edit, te::AudioTrack& track, const char* xmlType)
    {
        auto plugin = edit.getPluginCache().createNewPlugin (xmlType, {});
        if (plugin != nullptr)
            track.pluginList.insertPlugin (plugin, track.pluginList.size(), nullptr);
        return plugin;
    }
}

TEST_CASE ("T1.1: getAutomatableParams — Volume/Pan + Plugin-Params, qualifiziert", "[automation][t11]")
{
    EditManager em;
    em.createNewEdit();
    auto* track = te::getAudioTracks (*em.getEdit())[0];
    REQUIRE (track != nullptr);

    AutomationEngine ae (em);

    // Vor Plugin-Insert: genau Volume + Pan.
    auto base = ae.getAutomatableParams (*track);
    REQUIRE (base.size() == 2);
    CHECK (base[0].id == "volume");
    CHECK (base[1].id == "pan");

    // TE-4-Band-EQ einfügen → Parameterliste wächst um dessen Params.
    auto eq = insertTePlugin (*em.getEdit(), *track, te::FourOscPlugin::xmlTypeName);
    if (eq == nullptr)   // Fallback: TE-Kompressor, falls 4osc nicht baubar
        eq = insertTePlugin (*em.getEdit(), *track, te::CompressorPlugin::xmlTypeName);
    REQUIRE (eq != nullptr);

    auto withPlugin = ae.getAutomatableParams (*track);
    REQUIRE (withPlugin.size() > 2);

    // Alle Plugin-Einträge sind qualifiziert und tragen den Plugin-Namen.
    for (int i = 2; i < withPlugin.size(); ++i)
    {
        CHECK (withPlugin[i].id.startsWith ("plug:" + eq->itemID.toString() + ":"));
        CHECK (withPlugin[i].displayName.isNotEmpty());
    }
}

TEST_CASE ("T1.1: qualifizierte IDs unterscheiden ZWEI Instanzen desselben Plugins", "[automation][t11]")
{
    EditManager em;
    em.createNewEdit();
    auto* track = te::getAudioTracks (*em.getEdit())[0];
    AutomationEngine ae (em);

    auto a = insertTePlugin (*em.getEdit(), *track, te::CompressorPlugin::xmlTypeName);
    auto b = insertTePlugin (*em.getEdit(), *track, te::CompressorPlugin::xmlTypeName);
    REQUIRE (a != nullptr);
    REQUIRE (b != nullptr);
    REQUIRE (a->itemID != b->itemID);

    const auto params = a->getAutomatableParameters();
    REQUIRE (! params.isEmpty());
    const auto pid = params[0]->paramID;

    auto* pa = AutomationEngine::resolveParameter (*track, "plug:" + a->itemID.toString() + ":" + pid);
    auto* pb = AutomationEngine::resolveParameter (*track, "plug:" + b->itemID.toString() + ":" + pid);

    REQUIRE (pa != nullptr);
    REQUIRE (pb != nullptr);
    CHECK (pa != pb);                        // gleiche paramID, verschiedene Instanzen
    CHECK (&pa->getPlugin()->edit == em.getEdit());

    // Unbekannte Instanz-ID → sauber nullptr (kein Name-Fallback auf die falsche Instanz).
    CHECK (AutomationEngine::resolveParameter (*track, "plug:9999:" + pid) == nullptr);
}

TEST_CASE ("T1.1: Automation-Punkte auf einem Plugin-Parameter (nativ, undobar)", "[automation][t11]")
{
    EditManager em;
    em.createNewEdit();
    auto* track = te::getAudioTracks (*em.getEdit())[0];
    AutomationEngine ae (em);

    auto comp = insertTePlugin (*em.getEdit(), *track, te::CompressorPlugin::xmlTypeName);
    REQUIRE (comp != nullptr);

    const auto params = comp->getAutomatableParameters();
    REQUIRE (! params.isEmpty());
    auto* param = params[0];
    const auto qid = "plug:" + comp->itemID.toString() + ":" + param->paramID;

    const auto range = param->getValueRange();
    const float vLow  = range.getStart();
    const float vHigh = range.getEnd();

    ae.addAutomationPoint (*track, qid, 1.0, vLow);
    ae.addAutomationPoint (*track, qid, 3.0, vHigh);

    REQUIRE (param->getCurve().getNumPoints() == 2);
    CHECK (ae.getValueAtTime (*track, qid, 1.0) == Catch::Approx (vLow));
    CHECK (ae.getValueAtTime (*track, qid, 3.0) == Catch::Approx (vHigh));
    // Interpolation in der Mitte liegt strikt zwischen den Extremen.
    const float mid = ae.getValueAtTime (*track, qid, 2.0);
    CHECK (mid >= vLow);
    CHECK (mid <= vHigh);

    // Undo entfernt den letzten Punkt (jeder Punkt = eigene Transaktion).
    em.getUndoManager().undo();
    CHECK (param->getCurve().getNumPoints() == 1);
}

TEST_CASE ("T1.1: toNorm/fromNorm — Pan-Range -1..1 korrekt (alter 0..1-Bug)", "[automation][t11]")
{
    EditManager em;
    em.createNewEdit();
    auto* track = te::getAudioTracks (*em.getEdit())[0];

    auto* pan = AutomationEngine::resolveParameter (*track, "pan");
    REQUIRE (pan != nullptr);
    REQUIRE (pan->getValueRange().getStart() == Catch::Approx (-1.0f));

    CHECK (AutomationEngine::toNorm (*pan, -1.0f) == Catch::Approx (0.0f));
    CHECK (AutomationEngine::toNorm (*pan,  0.0f) == Catch::Approx (0.5f));
    CHECK (AutomationEngine::toNorm (*pan,  1.0f) == Catch::Approx (1.0f));
    CHECK (AutomationEngine::fromNorm (*pan, 0.0f) == Catch::Approx (-1.0f));
    CHECK (AutomationEngine::fromNorm (*pan, 0.5f) == Catch::Approx (0.0f));
    CHECK (AutomationEngine::fromNorm (*pan, 1.0f) == Catch::Approx (1.0f));

    // Roundtrip auf dem Volume-Param (0..1) bleibt identisch.
    auto* vol = AutomationEngine::resolveParameter (*track, "volume");
    REQUIRE (vol != nullptr);
    CHECK (AutomationEngine::fromNorm (*vol, AutomationEngine::toNorm (*vol, 0.7f))
           == Catch::Approx (0.7f));
}
