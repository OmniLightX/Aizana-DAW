#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <functional>

// The owner must detach before destroying the current Edit/UndoManager.
// Notifications run on the message thread, like native menu updates.
class UndoMenuObserver final : private juce::ChangeListener
{
public:
    explicit UndoMenuObserver (std::function<void()> refresh) : refreshMenu (std::move (refresh)) {}
    ~UndoMenuObserver() override { detach(); }

    void bind (juce::UndoManager* next)
    {
        if (manager == next)
            return;
        detach();
        manager = next;
        if (manager != nullptr)
            manager->addChangeListener (this);
        refreshMenu();
    }

    void detach()
    {
        if (manager != nullptr)
            manager->removeChangeListener (this);
        manager = nullptr;
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override
    {
        if (source == manager)
            refreshMenu();
    }

    juce::UndoManager* manager = nullptr;
    std::function<void()> refreshMenu;
    JUCE_DECLARE_NON_COPYABLE (UndoMenuObserver)
};
