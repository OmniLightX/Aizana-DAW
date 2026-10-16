#include "engine/EditManager.h"
#include "plugins/FxChainOperations.h"
#include <catch2/catch_test_macros.hpp>
using Ops = aizana::FxChainOperations;
static te::Plugin::Ptr addFx(te::Edit &edit, te::PluginList &list)
{
    auto p = edit.getPluginCache().createNewPlugin(te::DelayPlugin::xmlTypeName, juce::PluginDescription{});
    list.insertPlugin(p, -1, nullptr);
    return p;
}
TEST_CASE("FX chain copy retains state and independent IDs with atomic undo", "[fxchain]")
{
    EditManager em;
    em.createNewEdit();
    auto &e = *em.getEdit();
    auto &src = te::getAudioTracks(e)[0]->pluginList;
    auto &dst = e.getMasterPluginList();
    auto p = addFx(e, src);
    REQUIRE(p != nullptr);
    p->state.setProperty("testSetting", 37, nullptr);
    p->setEnabled(false);
    const auto snap = Ops::snapshot(src);
    const int before = dst.size();
    e.getUndoManager().clearUndoHistory();
    REQUIRE(Ops::apply(dst, snap).wasOk());
    auto copy = Ops::effects(dst);
    REQUIRE(copy.size() == 1);
    CHECK(copy[0]->itemID != p->itemID);
    CHECK((int)copy[0]->state.getProperty("testSetting") == 37);
    CHECK_FALSE(copy[0]->isEnabled());
    copy[0]->state.setProperty("testSetting", 99, nullptr);
    CHECK((int)p->state.getProperty("testSetting") == 37);
    e.getUndoManager().beginNewTransaction();
    REQUIRE(e.getUndoManager().undo());
    CHECK(dst.size() == before);
    REQUIRE(e.getUndoManager().redo());
    CHECK(Ops::effects(dst).size() == 1);
}
TEST_CASE("FX chain bulk operations protect internals and undo once", "[fxchain]")
{
    EditManager em;
    em.createNewEdit();
    auto &e = *em.getEdit();
    auto &list = te::getAudioTracks(e)[0]->pluginList;
    const int internals = list.size();
    addFx(e, list);
    addFx(e, list);
    e.getUndoManager().clearUndoHistory();
    Ops::setEnabled(list, false);
    for (auto *p : Ops::effects(list))
        CHECK_FALSE(p->isEnabled());
    e.getUndoManager().beginNewTransaction();
    REQUIRE(e.getUndoManager().undo());
    for (auto *p : Ops::effects(list))
        CHECK(p->isEnabled());
    Ops::removeAll(list);
    CHECK(list.size() == internals);
    e.getUndoManager().beginNewTransaction();
    REQUIRE(e.getUndoManager().undo());
    CHECK(Ops::effects(list).size() == 2);
}
TEST_CASE("FX chain malformed input cannot erase target and presets roundtrip", "[fxchain]")
{
    EditManager em;
    em.createNewEdit();
    auto &e = *em.getEdit();
    auto &list = te::getAudioTracks(e)[0]->pluginList;
    addFx(e, list);
    auto snap = Ops::snapshot(list);
    auto bad = snap.createCopy();
    bad.setProperty("version", 999, nullptr);
    CHECK(Ops::apply(list, bad, -1, true).failed());
    CHECK(Ops::effects(list).size() == 1);
    auto unknown = juce::ValueTree(te::IDs::PLUGIN);
    unknown.setProperty(te::IDs::type, "missing-test-plugin", nullptr);
    bad = snap.createCopy();
    bad.addChild(unknown, -1, nullptr);
    CHECK(Ops::apply(list, bad, -1, true).failed());
    CHECK(Ops::effects(list).size() == 1);
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile("aizana-chain", ".aizfxchain");
    REQUIRE(Ops::save(file, snap).wasOk());
    juce::ValueTree loaded;
    REQUIRE(Ops::load(file, loaded).wasOk());
    CHECK(loaded.isEquivalentTo(snap));
    file.deleteFile();
    REQUIRE(Ops::apply(list, loaded, -1, true).wasOk());
    CHECK(Ops::effects(list).size() == 1);
}

#include "plugins/BuiltInPluginFormat.h"
TEST_CASE("FX chain copies real processor settings while keeping instruments and sends", "[fxchain]")
{
    EditManager em;
    em.createNewEdit();
    auto &e = *em.getEdit();
    auto &list = te::getAudioTracks(e)[0]->pluginList;
    auto instrument =
        e.getPluginCache().createNewPlugin(te::SamplerPlugin::xmlTypeName, juce::PluginDescription{});
    auto send = e.getPluginCache().createNewPlugin(te::AuxSendPlugin::xmlTypeName, juce::PluginDescription{});
    auto ret =
        e.getPluginCache().createNewPlugin(te::AuxReturnPlugin::xmlTypeName, juce::PluginDescription{});
    for (auto p : {instrument, send, ret})
    {
        REQUIRE(p);
        list.insertPlugin(p, -1, nullptr);
    }
    auto desc = BuiltInPluginFormat::getDescriptionFor("Aizana Compressor");
    auto fx = e.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, desc);
    REQUIRE(fx);
    list.insertPlugin(fx, -1, nullptr);
    auto *ext = dynamic_cast<te::ExternalPlugin *>(fx.get());
    REQUIRE(ext);
    auto *instance = ext->getAudioPluginInstance();
    REQUIRE(instance);
    REQUIRE(instance->getParameters().size() > 0);
    instance->getParameters()[0]->setValueNotifyingHost(0.37f);
    auto snap = Ops::snapshot(list);
    REQUIRE(snap.getNumChildren() == 1);
    auto &master = e.getMasterPluginList();
    REQUIRE(Ops::apply(master, snap).wasOk());
    auto *copy = dynamic_cast<te::ExternalPlugin *>(Ops::effects(master)[0]);
    REQUIRE(copy);
    REQUIRE(copy->getAudioPluginInstance());
    CHECK(std::abs(copy->getAudioPluginInstance()->getParameters()[0]->getValue() - 0.37f) < 0.001f);
    Ops::removeAll(list);
    CHECK(list.contains(instrument.get()));
    CHECK(list.contains(send.get()));
    CHECK(list.contains(ret.get()));
    CHECK(Ops::effects(list).isEmpty());
}
TEST_CASE("FX chain insertion order and replace undo preserve complete destination", "[fxchain]")
{
    EditManager em;
    em.createNewEdit();
    auto &e = *em.getEdit();
    auto &list = te::getAudioTracks(e)[0]->pluginList;
    auto a = addFx(e, list);
    auto b = addFx(e, list);
    a->state.setProperty("testSetting", 1, nullptr);
    b->state.setProperty("testSetting", 2, nullptr);
    auto snap = Ops::snapshot(list, a.get());
    REQUIRE(Ops::apply(list, snap, 1).wasOk());
    auto fx = Ops::effects(list);
    REQUIRE(fx.size() == 3);
    CHECK(fx[0] == a.get());
    CHECK(fx[2] == b.get());
    CHECK(fx[1]->itemID != a->itemID);
    e.getUndoManager().clearUndoHistory();
    REQUIRE(Ops::apply(list, snap, -1, true).wasOk());
    CHECK(Ops::effects(list).size() == 1);
    e.getUndoManager().beginNewTransaction();
    REQUIRE(e.getUndoManager().undo());
    fx = Ops::effects(list);
    REQUIRE(fx.size() == 3);
    CHECK(fx[0]->itemID == a->itemID);
    CHECK(fx[2]->itemID == b->itemID);
    REQUIRE(e.getUndoManager().redo());
    CHECK(Ops::effects(list).size() == 1);
}
TEST_CASE("FX chain true move preserves ID automation and sidechain", "[fxchain]")
{
    EditManager em;
    em.createNewEdit();
    auto &e = *em.getEdit();
    auto &src = te::getAudioTracks(e)[0]->pluginList;
    auto target = e.insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(e), nullptr);
    REQUIRE(target);
    auto p = addFx(e, src);
    const auto id = p->itemID;
    p->state.setProperty(te::IDs::sidechainSourceID, te::getAudioTracks(e)[0]->itemID.toString(), nullptr);
    auto automation = juce::ValueTree("AUTOMATIONCURVE");
    automation.setProperty("test", 1, nullptr);
    p->state.addChild(automation, -1, nullptr);
    const auto before = p->state.createCopy();
    e.getUndoManager().clearUndoHistory();
    REQUIRE(Ops::move(src, target->pluginList, p.get(), 0).wasOk());
    CHECK_FALSE(src.contains(p.get()));
    CHECK(target->pluginList.contains(p.get()));
    CHECK(p->itemID == id);
    CHECK(p->state.isEquivalentTo(before));
    e.getUndoManager().beginNewTransaction();
    REQUIRE(e.getUndoManager().undo());
    CHECK(src.contains(p.get()));
    CHECK_FALSE(target->pluginList.contains(p.get()));
    REQUIRE(e.getUndoManager().redo());
    CHECK(target->pluginList.contains(p.get()));
}
TEST_CASE("FX reorder interprets insertion boundaries consistently", "[fxchain]")
{
    EditManager em;
    em.createNewEdit();
    auto &e = *em.getEdit();
    auto &l = te::getAudioTracks(e)[0]->pluginList;
    auto a = addFx(e, l), b = addFx(e, l), c = addFx(e, l);
    Ops::reorder(l, *a, 1);
    CHECK(Ops::effects(l)[0] == a.get());
    Ops::reorder(l, *a, 2);
    CHECK(Ops::effects(l)[0] == b.get());
    CHECK(Ops::effects(l)[1] == a.get());
    CHECK(Ops::effects(l)[2] == c.get());
    Ops::reorder(l, *a, 3);
    CHECK(Ops::effects(l)[2] == a.get());
    Ops::reorder(l, *a, 0);
    CHECK(Ops::effects(l)[0] == a.get());
}
TEST_CASE("FX insertion remains pre-fader on empty rack and before trailing internals", "[fxchain]")
{
    EditManager em;
    em.createNewEdit();
    auto &e = *em.getEdit();
    auto &list = te::getAudioTracks(e)[0]->pluginList;
    REQUIRE(list.size() > 0);
    CHECK(Ops::insertionIndex(list) == 0);
    auto p = e.getPluginCache().createNewPlugin(te::DelayPlugin::xmlTypeName, juce::PluginDescription{});
    list.insertPlugin(p, 0, nullptr);
    CHECK(Ops::insertionIndex(list) == 1);
    CHECK(Ops::insertionIndex(list, 0) == 0);
    auto q = e.getPluginCache().createNewPlugin(te::DelayPlugin::xmlTypeName, juce::PluginDescription{});
    list.insertPlugin(q, Ops::insertionIndex(list), nullptr);
    CHECK(list[0] == p.get());
    CHECK(list[1] == q.get());
}
