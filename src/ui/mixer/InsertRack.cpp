#include "InsertRack.h"
#include "engine/MixerEngine.h"
#include "plugins/BuiltInPluginFormat.h"
#include "plugins/FxChainOperations.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "ui/pluginbrowser/PluginBrowserPopup.h"
#include "ui/pluginwin/PluginWindowManager.h"
using Ops = aizana::FxChainOperations;
juce::ValueTree InsertRack::clipboard;

struct InsertRack::Row : juce::Component
{
    InsertRack &rack;
    te::EditItemID id;
    juce::String name;
    bool enabled = true, bypassGesture = false;
    Row(InsertRack &r, te::Plugin *p)
        : rack(r), id(p ? p->itemID : te::EditItemID{}), name(p ? p->getName() : "+")
    {
        enabled = p ? p->isEnabled() : true;
        setTitle("Insert: " + name);
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colour(enabled ? 0xff292d31 : 0xff1b1e21));
        g.setColour(juce::Colour(0xff50565b));
        g.drawRect(getLocalBounds());
        g.setColour(juce::Colour(enabled ? 0xffeeeeee : 0xff888888));
        g.setFont(juce::FontOptions(11.f));
        g.drawText(name, getLocalBounds().reduced(4, 0).withTrimmedRight(12),
                   juce::Justification::centredLeft, true);
        if (id.isValid())
        {
            g.setColour(enabled ? juce::Colour(0xff48bca7) : juce::Colour(0xff666666));
            g.fillEllipse((float)getWidth() - 10, 6, 6, 6);
        }
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        bypassGesture = false;
        if (e.mods.isPopupMenu())
        {
            rack.showPluginMenu(id);
            return;
        }
        if (!id.isValid())
        {
            rack.addPlugin();
            return;
        }
        if (e.x >= getWidth() - 14)
        {
            bypassGesture = true;
            if (auto *p = rack.find(id))
            {
                auto &um = rack.list.getEdit().getUndoManager();
                um.beginNewTransaction("Insert aktivieren/deaktivieren");
                p->state.setProperty(te::IDs::enabled, !p->isEnabled(), &um);
            }
        }
    }
    void mouseDoubleClick(const juce::MouseEvent &) override
    {
        if (auto *p = rack.find(id))
            rack.windows.openPluginWindow(*p);
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (!bypassGesture && e.getDistanceFromDragStart() > 5)
            if (auto *p = rack.find(id))
                rack.startDrag(p);
    }
};
void InsertRack::HeaderButton::paintButton(juce::Graphics &g, bool over, bool down)
{
    if (over || down)
    {
        g.setColour(juce::Colours::white.withAlpha(down ? 0.18f : 0.09f));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.f), 3.f);
    }
    g.setColour((getToggleState() ? juce::Colour(0xff48bca7) : juce::Colour(0xffcccccc))
                    .withMultipliedAlpha(isEnabled() ? 1.f : 0.4f));
    const float x = getWidth() * 0.5f, y = getHeight() * 0.5f;
    if (icon == Plus)
    {
        g.drawLine(x - 4.f, y, x + 4.f, y, 1.6f);
        g.drawLine(x, y - 4.f, x, y + 4.f, 1.6f);
    }
    else if (icon == Menu)
    {
        juce::Path p;
        p.addTriangle(x - 4.f, y - 2.f, x + 4.f, y - 2.f, x, y + 3.f);
        g.fillPath(p);
    }
    else
    {
        juce::Path p;
        p.addCentredArc(x, y, 5.f, 5.f, 0.f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
        g.strokePath(p, juce::PathStrokeType(1.6f));
        g.drawLine(x, y - 6.f, x, y, 1.6f);
    }
}

InsertRack::InsertRack(te::PluginList &l, PluginWindowManager &w, PluginScanner &s, MixerEngine *m)
    : list(l), windows(w), scanner(s), mixer(m)
{
    power.setButtonText("Inserts ein/aus");
    menu.setButtonText("FX-Kettenmenue");
    plus.setButtonText("Plugin hinzufuegen");
    power.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff23699b));
    title.setText("Inserts", juce::dontSendNotification);
    title.setFont(juce::FontOptions(11.f));
    title.setInterceptsMouseClicks(false, false);
    for (auto *c : std::initializer_list<juce::Component *>{&power, &title, &menu, &plus, &viewport})
        addAndMakeVisible(c);
    power.setTitle("Alle Inserts aktivieren oder deaktivieren");
    menu.setTitle("FX-Kettenmenue");
    plus.setTitle("Plugin hinzufuegen");
    power.setClickingTogglesState(false);
    power.onClick = [this]
    {
        bool any = false;
        for (auto *p : Ops::effects(list))
            any |= p->isEnabled();
        Ops::setEnabled(list, !any);
    };
    menu.onClick = [this] { showMenu(); };
    plus.onClick = [this] { addPlugin(); };
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    list.state.addListener(this);
    rebuild();
}
InsertRack::~InsertRack()
{
    list.state.removeListener(this);
    cancelPendingUpdate();
}
void InsertRack::refresh()
{
    triggerAsyncUpdate();
}
void InsertRack::handleAsyncUpdate()
{
    rebuild();
}
void InsertRack::rebuild()
{
    rows.clear();
    bool any = false;
    for (auto *p : list)
    {
        const auto t = p->getPluginType();
        if (t == "volume" || t == "level" || t == "auxreturn" || t == "auxsend")
            continue;
        auto *r = rows.add(new Row(*this, p));
        content.addAndMakeVisible(r);
        if (Ops::isEffect(*p))
            any |= p->isEnabled();
    }
    content.addAndMakeVisible(rows.add(new Row(*this, nullptr)));
    power.setToggleState(any, juce::dontSendNotification);
    power.setEnabled(!Ops::effects(list).isEmpty());
    resized();
    repaint();
}
void InsertRack::resized()
{
    auto a = getLocalBounds();
    auto h = a.removeFromTop(kHeaderHeight);
    power.setBounds(h.removeFromLeft(20));
    plus.setBounds(h.removeFromRight(20));
    menu.setBounds(h.removeFromRight(18));
    title.setBounds(h);
    viewport.setBounds(a);
    const int width = juce::jmax(
        1, a.getWidth() - (rows.size() * kSlotHeight > a.getHeight() ? viewport.getScrollBarThickness() : 0));
    content.setSize(width, juce::jmax(kSlotHeight, rows.size() * kSlotHeight));
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds(0, i * kSlotHeight, width, kSlotHeight);
}
void InsertRack::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colour(0xff181b1e));
}
void InsertRack::paintOverChildren(juce::Graphics &g)
{
    if (dropSlot < 0)
        return;
    g.reduceClipRegion(viewport.getBounds());
    g.setColour(juce::Colour(0xff44aaff));
    auto fx = Ops::effects(list);
    int row = rows.size() - 1;
    if (dropSlot < fx.size())
        for (int i = 0; i < rows.size(); ++i)
            if (rows[i]->id == fx[dropSlot]->itemID)
                row = i;
    const int y = getLocalPoint(&content, juce::Point<int>(0, row * kSlotHeight)).y;
    g.fillRect(0, y, getWidth(), 2);
}
te::Plugin *InsertRack::find(te::EditItemID id) const
{
    for (auto *p : list)
        if (p->itemID == id)
            return p;
    return nullptr;
}
void InsertRack::mouseDrag(const juce::MouseEvent &e)
{
    if (e.mouseDownPosition.y < kHeaderHeight && e.getDistanceFromDragStart() > 5)
        startDrag(nullptr);
}
void InsertRack::startDrag(te::Plugin *p)
{
    if (p && !Ops::isEffect(*p))
        return;
    if (auto *d = juce::DragAndDropContainer::findParentDragContainerFor(this))
    {
        if (d->isDragAndDropActive())
            return;
        const auto snap = Ops::snapshot(list, p);
        if (snap.getNumChildren() == 0)
            return;
        auto *data = new juce::DynamicObject();
        data->setProperty("kind", "AizanaFX");
        data->setProperty("xml", snap.toXmlString());
        data->setProperty("id", p ? p->itemID.toString() : juce::String());
        d->startDragging(juce::var(data), this);
    }
}
bool InsertRack::isInterestedInDragSource(const SourceDetails &d)
{
    auto *source = dynamic_cast<InsertRack *>(d.sourceComponent.get());
    return source && &source->list.getEdit() == &list.getEdit() &&
           d.description.getProperty("kind", {}) == "AizanaFX";
}
void InsertRack::itemDragMove(const SourceDetails &d)
{
    if (auto* outer = findParentComponentOfClass<juce::Viewport>())
    {
        const auto position = outer->getLocalPoint(this, d.localPosition);
        outer->autoScroll(position.x, position.y, 24, 16);
    }
    dropSlot = 0;
    const int y = getLocalPoint(this, d.localPosition).y - kHeaderHeight + viewport.getViewPositionY();
    const int row = juce::jmax(0, y / kSlotHeight);
    for (int i = 0; i < juce::jmin(row, rows.size()); ++i)
        if (auto *p = find(rows[i]->id))
            if (Ops::isEffect(*p))
                ++dropSlot;
    if (d.localPosition.y < kHeaderHeight + 12)
        viewport.setViewPosition(0, juce::jmax(0, viewport.getViewPositionY() - kSlotHeight));
    if (d.localPosition.y > getHeight() - 12)
        viewport.setViewPosition(0, viewport.getViewPositionY() + kSlotHeight);
    repaint();
}
void InsertRack::itemDragExit(const SourceDetails &)
{
    dropSlot = -1;
    repaint();
}
void InsertRack::itemDropped(const SourceDetails &d)
{
    itemDragMove(d);
    const int slot = dropSlot;
    dropSlot = -1;
    repaint();
    auto *source = dynamic_cast<InsertRack *>(d.sourceComponent.get());
    if (!source)
        return;
    auto xml = juce::XmlDocument::parse(d.description.getProperty("xml", {}).toString());
    if (!xml)
        return;
    const auto snapshot = juce::ValueTree::fromXml(*xml);
    const auto id = te::EditItemID::fromString(d.description.getProperty("id", {}).toString());
    const bool move = juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown();
    juce::Component::SafePointer<InsertRack> target(this), src(source);
    juce::MessageManager::callAsync(
        [target, src, snapshot, id, slot, move]
        {
            if (!target || !src)
                return;
            if (&target->list == &src->list)
            {
                auto *p = src->find(id);
                if (!p)
                    return; // whole-chain self-drop is a no-op
                Ops::reorder(target->list, *p, slot);
                return;
            }
            if (id.isValid() && !src->find(id))
                return;
            auto result =
                move ? Ops::move(src->list, target->list, id.isValid() ? src->find(id) : nullptr, slot)
                     : Ops::apply(target->list, snapshot, slot);
            target->report(result);
        });
}
void InsertRack::report(juce::Result r)
{
    if (r.failed())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "FX-Kette",
                                               r.getErrorMessage());
}
void InsertRack::apply(const juce::ValueTree &v, int slot, bool replace)
{
    for (const auto &d : scanner.getKnownPlugins().getTypes())
        list.getEdit().engine.getPluginManager().knownPluginList.addType(
            PluginManager::hostingDescription(d));
    if (replace)
        for (auto *p : Ops::effects(list))
            windows.closePluginWindowNow(*p);
    report(Ops::apply(list, v, slot, replace));
}
void InsertRack::showMenu()
{
    juce::PopupMenu m;
    const bool has = !Ops::effects(list).isEmpty();
    m.addItem(1, "FX-Kette kopieren", has);
    m.addItem(2, "FX-Kette einfuegen", clipboard.isValid());
    m.addItem(3, "Durch kopierte FX-Kette ersetzen", clipboard.isValid());
    m.addSeparator();
    m.addItem(4, "FX-Kette speichern...", has);
    m.addItem(5, "FX-Kette laden...");
    m.addItem(6, "Durch gespeicherte FX-Kette ersetzen...");
    m.addItem(7, "Als Default-Preset speichern", has);
    m.addItem(8, "Default-Preset laden",
              Ops::presetDirectory().getChildFile("default.aizfxchain").existsAsFile());
    m.addSeparator();
    m.addItem(9, "Alle aktivieren", has);
    m.addItem(10, "Alle deaktivieren", has);
    m.addItem(11, "Alle entfernen", has);
    juce::Component::SafePointer<InsertRack> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menu),
                    [safe](int r)
                    {
                        if (!safe)
                            return;
                        switch (r)
                        {
                        case 1:
                            clipboard = Ops::snapshot(safe->list);
                            break;
                        case 2:
                            safe->apply(clipboard);
                            break;
                        case 3:
                            safe->apply(clipboard, -1, true);
                            break;
                        case 4:
                            safe->chooseFile(true);
                            break;
                        case 5:
                            safe->chooseFile(false);
                            break;
                        case 6:
                            safe->chooseFile(false, true);
                            break;
                        case 7:
                            safe->report(Ops::save(Ops::presetDirectory().getChildFile("default.aizfxchain"),
                                                   Ops::snapshot(safe->list)));
                            break;
                        case 8:
                        {
                            juce::ValueTree v;
                            auto result =
                                Ops::load(Ops::presetDirectory().getChildFile("default.aizfxchain"), v);
                            if (result.wasOk())
                                safe->apply(v);
                            else
                                safe->report(result);
                            break;
                        }
                        case 9:
                            Ops::setEnabled(safe->list, true);
                            break;
                        case 10:
                            Ops::setEnabled(safe->list, false);
                            break;
                        case 11:
                            for (auto *p : Ops::effects(safe->list))
                                safe->windows.closePluginWindowNow(*p);
                            Ops::removeAll(safe->list);
                            break;
                        default:
                            break;
                        }
                    });
}
void InsertRack::chooseFile(bool save, bool replace)
{
    chooser = std::make_unique<juce::FileChooser>(
        save ? "FX-Kette speichern" : "FX-Kette laden",
        Ops::presetDirectory().getChildFile("Meine FX-Kette.aizfxchain"), "*.aizfxchain");
    const auto snapshot = save ? Ops::snapshot(list) : juce::ValueTree{};
    juce::Component::SafePointer<InsertRack> safe(this);
    chooser->launchAsync(
        (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
              : juce::FileBrowserComponent::openMode) |
            juce::FileBrowserComponent::canSelectFiles,
        [safe, save, replace, snapshot](const juce::FileChooser &fc)
        {
            if (!safe)
                return;
            auto f = fc.getResult();
            if (f == juce::File{})
                return;
            if (save)
                safe->report(Ops::save(f.withFileExtension("aizfxchain"), snapshot));
            else
            {
                juce::ValueTree v;
                auto r = Ops::load(f, v);
                if (r.wasOk())
                    safe->apply(v, -1, replace);
                else
                    safe->report(r);
            }
        });
}
void InsertRack::addPlugin()
{
    juce::PopupMenu m;
    juce::OwnedArray<juce::PluginDescription> built;
    BuiltInPluginFormat::getBuiltInDescriptions(built);
    juce::Array<juce::PluginDescription> descriptions;
    for (auto *d : built)
    {
        descriptions.add(*d);
        m.addItem(descriptions.size(), d->name);
    }
    m.addSeparator();
    m.addItem(1000, "Plugins durchsuchen...");
    juce::Component::SafePointer<InsertRack> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&plus),
                    [safe, descriptions](int r)
                    {
                        if (!safe)
                            return;
                        if (r == 1000)
                            PluginBrowserPopup::showForComponent(*safe, safe->scanner.getKnownPlugins(),
                                                                 [safe](const juce::PluginDescription &d)
                                                                 {
                                                                     if (safe)
                                                                         safe->insertDescription(d);
                                                                 });
                        else if (r > 0 && r <= descriptions.size())
                            safe->insertDescription(descriptions[r - 1]);
                    });
}
void InsertRack::insertDescription(const juce::PluginDescription &d)
{
    const auto desc = PluginManager::hostingDescription(d);
    auto &e = list.getEdit();
    e.engine.getPluginManager().knownPluginList.addType(desc);
    auto p = e.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, desc);
    if (!p || p->isMissing())
    {
        report(juce::Result::fail("Plugin konnte nicht geladen werden."));
        return;
    }
    e.getUndoManager().beginNewTransaction("Insert hinzufuegen");
    list.insertPlugin(p, Ops::insertionIndex(list), nullptr);
    if (list.contains(p.get()))
        windows.openPluginWindow(*p);
}
void InsertRack::showPluginMenu(te::EditItemID id)
{
    auto *p = find(id);
    if (!p)
        return;
    juce::PopupMenu m;
    m.addItem(1, "Plugin kopieren", Ops::isEffect(*p));
    m.addItem(2, "Plugin entfernen");
    juce::Array<te::EditItemID> sources;
    if (mixer && p->canSidechain())
    {
        juce::PopupMenu sub;
        sub.addItem(3, "Keine", true, !p->getSidechainSourceID().isValid());
        for (auto *t : te::getAudioTracks(list.getEdit()))
            if (t != list.getOwnerTrack())
            {
                sources.add(t->itemID);
                sub.addItem(99 + sources.size(), t->getName(), true, p->getSidechainSourceID() == t->itemID);
            }
        m.addSubMenu("Sidechain-Quelle", sub);
    }
    juce::Component::SafePointer<InsertRack> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                    [safe, id, sources](int r)
                    {
                        if (!safe)
                            return;
                        auto *plugin = safe->find(id);
                        if (!plugin)
                            return;
                        if (r == 1)
                            clipboard = Ops::snapshot(safe->list, plugin);
                        else if (r == 2)
                        {
                            auto &um = safe->list.getEdit().getUndoManager();
                            um.beginNewTransaction("Insert entfernen");
                            safe->windows.closePluginWindowNow(*plugin);
                            safe->list.state.removeChild(plugin->state, &um);
                        }
                        else if (r == 3 && safe->mixer)
                            safe->mixer->clearSidechainSource(*plugin);
                        else if (r >= 100 && r - 100 < sources.size() && safe->mixer)
                            for (auto *t : te::getAudioTracks(safe->list.getEdit()))
                                if (t->itemID == sources[r - 100])
                                    safe->mixer->setSidechainSource(*plugin, *t);
                    });
}
