#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
class PluginScanner;
class PluginWindowManager;
class MixerEngine;
namespace te = tracktion::engine;

/** One insert UI for audio, bus, FX and true master lists. */
class InsertRack : public juce::Component,
                   public juce::DragAndDropTarget,
                   private juce::ValueTree::Listener,
                   private juce::AsyncUpdater
{
  public:
    InsertRack(te::PluginList &, PluginWindowManager &, PluginScanner &, MixerEngine * = nullptr);
    ~InsertRack() override;
    static constexpr int kVisibleSlots = 6, kSlotHeight = 18, kHeaderHeight = 24;
    void refresh();
    void resized() override;
    void paint(juce::Graphics &) override;
    void paintOverChildren(juce::Graphics &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    void focusOfChildComponentChanged(FocusChangeType) override { repaint(); }
    bool isInterestedInDragSource(const SourceDetails &) override;
    void itemDragMove(const SourceDetails &) override;
    void itemDragExit(const SourceDetails &) override;
    void itemDropped(const SourceDetails &) override;

  private:
    te::PluginList &list;
    PluginWindowManager &windows;
    PluginScanner &scanner;
    MixerEngine *mixer;
    struct HeaderButton : juce::TextButton
    {
        enum Icon { Power, Menu, Plus };
        explicit HeaderButton(Icon i) : icon(i) {}
        void paintButton(juce::Graphics &, bool, bool) override;
        Icon icon;
    };
    HeaderButton power{HeaderButton::Power}, menu{HeaderButton::Menu}, plus{HeaderButton::Plus};
    juce::Label title;
    juce::Viewport viewport;
    juce::Component content;
    struct Row;
    juce::OwnedArray<Row> rows;
    std::unique_ptr<juce::FileChooser> chooser;
    int dropSlot = -1;
    te::EditItemID selectedPlugin;
    static juce::ValueTree clipboard;
    void rebuild();
    void handleAsyncUpdate() override;
    void startDrag(te::Plugin *);
    void showMenu();
    void showPluginMenu(te::EditItemID);
    void addPlugin();
    void insertDescription(const juce::PluginDescription &);
    void chooseFile(bool save, bool replace = false);
    void report(juce::Result);
    void apply(const juce::ValueTree &, int slot = -1, bool replace = false);
    te::Plugin *find(te::EditItemID) const;
    void valueTreePropertyChanged(juce::ValueTree &, const juce::Identifier &) override
    {
        triggerAsyncUpdate();
    }
    void valueTreeChildAdded(juce::ValueTree &, juce::ValueTree &) override
    {
        triggerAsyncUpdate();
    }
    void valueTreeChildRemoved(juce::ValueTree &, juce::ValueTree &, int) override
    {
        triggerAsyncUpdate();
    }
    void valueTreeChildOrderChanged(juce::ValueTree &, int, int) override
    {
        triggerAsyncUpdate();
    }
    void valueTreeParentChanged(juce::ValueTree &) override {}
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InsertRack)
};
