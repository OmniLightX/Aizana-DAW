#include "FxChainOperations.h"

namespace aizana
{
namespace
{
juce::Result validate(const juce::ValueTree &v)
{
    if (!v.hasType("AIZANA_FX_CHAIN") || (int)v.getProperty("version") != 1)
        return juce::Result::fail("Keine unterstuetzte Aizana FX-Kette.");
    if (v.getNumChildren() == 0 || v.getNumChildren() > 256)
        return juce::Result::fail("Die FX-Kette ist leer oder zu gross.");
    for (auto c : v)
        if (!c.hasType(te::IDs::PLUGIN))
            return juce::Result::fail("Ungueltiger Ketteninhalt.");
    return juce::Result::ok();
}
void stripReferences(juce::ValueTree v)
{
    v.removeProperty(te::IDs::sidechainSourceID, nullptr);
    // Timeline curves belong to the song, not to a reusable sound preset.
    for (int i = v.getNumChildren(); --i >= 0;)
    {
        auto c = v.getChild(i);
        if (c.hasType("AUTOMATIONCURVE") || c.hasType("MODIFIERASSIGNMENTS"))
            v.removeChild(i, nullptr);
        else
            stripReferences(c);
    }
}
} // namespace
bool FxChainOperations::isEffect(te::Plugin &p)
{
    const auto t = p.getPluginType();
    return t != "volume" && t != "level" && t != "auxsend" && t != "auxreturn" && !p.isSynth();
}
juce::Array<te::Plugin *> FxChainOperations::effects(te::PluginList &list)
{
    juce::Array<te::Plugin *> result;
    for (auto *p : list)
        if (isEffect(*p))
            result.add(p);
    return result;
}
int FxChainOperations::insertionIndex(te::PluginList &list, int slot)
{
    auto fx = effects(list);
    if (slot >= 0 && slot < fx.size())
        return list.indexOf(fx[slot]);
    if (!fx.isEmpty())
        return list.indexOf(fx.getLast()) + 1;
    int index = 0;
    for (auto *p : list)
        if (p->getPluginType() == "auxreturn" || p->getPluginType() == "auxsend" || p->isSynth())
            index = list.indexOf(p) + 1;
    return index;
}
juce::ValueTree FxChainOperations::snapshot(te::PluginList &list, te::Plugin *single)
{
    juce::ValueTree v("AIZANA_FX_CHAIN");
    v.setProperty("version", 1, nullptr);
    for (auto *p : list)
        if (isEffect(*p) && (single == nullptr || single == p))
        {
            p->flushPluginStateToValueTree();
            auto copy = p->state.createCopy();
            stripReferences(copy);
            v.addChild(copy, -1, nullptr);
        }
    return v;
}
juce::Result FxChainOperations::apply(te::PluginList &list, const juce::ValueTree &data, int slot,
                                      bool replace)
{
    auto valid = validate(data);
    if (valid.failed())
        return valid;
    auto &edit = list.getEdit();
    auto copy = data.createCopy();
    stripReferences(copy);
    te::EditItemID::remapIDs(copy, nullptr, edit);
    te::Plugin::Array prepared;
    for (auto child : copy)
    {
        // A remapped preset is restored state, not a request for factory defaults.
        // This API also returns null normally for unavailable plugin types.
        auto p = edit.getPluginCache().getOrCreatePluginFor(child);
        if (p == nullptr || !isEffect(*p) || p->isMissing() ||
            (list.getOwnerTrack() == nullptr && !p->canBeAddedToMaster()) ||
            (list.getOwnerTrack() != nullptr && !list.getOwnerTrack()->canContainPlugin(p.get())))
            return juce::Result::fail("Ein Effekt fehlt oder kann auf diesem Kanal nicht geladen werden.");
        if (auto *ext = dynamic_cast<te::ExternalPlugin *>(p.get()))
            if (ext->getAudioPluginInstance() == nullptr)
                return juce::Result::fail("Plugin konnte nicht geladen werden: " + p->getName());
        prepared.add(p);
    }
    const auto old = effects(list);
    const auto limits = edit.engine.getEngineBehaviour().getEditLimits();
    const int limit = list.getOwnerTrack() != nullptr ? limits.maxPluginsOnTrack : limits.maxNumMasterPlugins;
    if (list.size() - (replace ? old.size() : 0) + prepared.size() > limit)
        return juce::Result::fail("Zu viele Inserts fuer diesen Kanal.");
    auto &um = edit.getUndoManager();
    um.beginNewTransaction(replace ? "FX-Kette ersetzen" : "FX-Kette einfuegen");
    if (replace)
        for (auto *p : old)
            list.state.removeChild(p->state, &um);
    int index = insertionIndex(list, slot);
    for (auto p : prepared)
        list.insertPlugin(p, index++, nullptr);
    return juce::Result::ok();
}
void FxChainOperations::reorder(te::PluginList &list, te::Plugin &plugin, int slot)
{
    auto fx = effects(list);
    const int from = fx.indexOf(&plugin);
    if (from < 0)
        return;
    slot = juce::jlimit(0, fx.size(), slot);
    const int to = slot > from ? slot - 1 : slot;
    if (to == from)
        return;
    auto &um = list.getEdit().getUndoManager();
    um.beginNewTransaction("Insert verschieben");
    list.state.moveChild(list.state.indexOf(plugin.state), list.state.indexOf(fx[to]->state), &um);
}
juce::Result FxChainOperations::move(te::PluginList &source, te::PluginList &target, te::Plugin *single,
                                     int slot)
{
    if (&source.getEdit() != &target.getEdit())
        return juce::Result::fail("Verschieben nur innerhalb derselben Session.");
    if (&source == &target)
    {
        if (single)
            reorder(source, *single, slot);
        return juce::Result::ok();
    }
    te::Plugin::Array selected;
    for (auto *p : effects(source))
        if (!single || single == p)
            selected.add(p);
    if (selected.isEmpty())
        return juce::Result::fail("Quell-Insert ist nicht mehr vorhanden.");
    for (auto p : selected)
        if ((!target.getOwnerTrack() && !p->canBeAddedToMaster()) ||
            (target.getOwnerTrack() && !target.getOwnerTrack()->canContainPlugin(p)) ||
            (target.getOwnerTrack() && p->getSidechainSourceID() == target.getOwnerTrack()->itemID))
            return juce::Result::fail("Dieses Insert kann nicht auf den Zielkanal verschoben werden.");
    const auto limits = target.getEdit().engine.getEngineBehaviour().getEditLimits();
    const int limit = target.getOwnerTrack() ? limits.maxPluginsOnTrack : limits.maxNumMasterPlugins;
    if (target.size() + selected.size() > limit)
        return juce::Result::fail("Zu viele Inserts auf dem Zielkanal.");
    int raw = insertionIndex(target, slot);
    auto &um = target.getEdit().getUndoManager();
    um.beginNewTransaction("Inserts auf anderen Kanal verschieben");
    for (auto p : selected)
        target.insertPlugin(p, raw++, nullptr);
    return juce::Result::ok();
}
void FxChainOperations::setEnabled(te::PluginList &list, bool enabled)
{
    auto &um = list.getEdit().getUndoManager();
    um.beginNewTransaction(enabled ? "Alle Inserts aktivieren" : "Alle Inserts deaktivieren");
    for (auto *p : effects(list))
        if (p->canBeDisabled())
            p->state.setProperty(te::IDs::enabled, enabled, &um);
}
void FxChainOperations::removeAll(te::PluginList &list)
{
    auto &um = list.getEdit().getUndoManager();
    um.beginNewTransaction("Alle Inserts entfernen");
    // Retain reference-counted plugins while their list entries are removed.
    te::Plugin::Array keep;
    for (auto *p : effects(list))
        keep.add(p);
    for (auto p : keep)
        list.state.removeChild(p->state, &um);
}
juce::Result FxChainOperations::save(const juce::File &file, const juce::ValueTree &v)
{
    auto r = validate(v);
    if (r.failed())
        return r;
    auto xml = v.createXml();
    if (!file.getParentDirectory().createDirectory())
        return juce::Result::fail("Preset-Ordner konnte nicht angelegt werden.");
    juce::TemporaryFile temp(file);
    if (!xml->writeTo(temp.getFile()) || !temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail("FX-Kette konnte nicht gespeichert werden.");
    return juce::Result::ok();
}
juce::Result FxChainOperations::load(const juce::File &file, juce::ValueTree &out)
{
    if (file.getSize() > 32 * 1024 * 1024)
        return juce::Result::fail("Preset-Datei ist zu gross.");
    auto xml = juce::XmlDocument::parse(file);
    auto v = xml ? juce::ValueTree::fromXml(*xml) : juce::ValueTree{};
    auto r = validate(v);
    if (r.wasOk())
        out = v;
    return r;
}
juce::File FxChainOperations::presetDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Aizana/FX Chains");
}
} // namespace aizana
