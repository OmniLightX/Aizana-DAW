#include "InputBusManager.h"
#include <vector>

//==============================================================================
InputBusManager::InputBusManager (EditManager& em)
    : editManager (em)
{
}

//==============================================================================
// Tree access
//==============================================================================

juce::ValueTree InputBusManager::getOrCreateBusesTree()
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return {};

    // Container created WITHOUT an UndoManager — it is a structural holder, not a
    // user action. Only the <INPUTBUS> children below it are undoable.
    return edit->state.getOrCreateChildWithName (kInputBusesTree, nullptr);
}

juce::ValueTree InputBusManager::readBusesTree() const
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return {};

    // getChildWithName never creates — returns an invalid tree if absent.
    return edit->state.getChildWithName (kInputBusesTree);
}

InputBusManager::InputBus InputBusManager::busFromNode (const juce::ValueTree& node)
{
    InputBus bus;
    bus.name      = node.getProperty (kInputBusName).toString();
    bus.role      = node.getProperty (kInputBusRole).toString();
    bus.hwChannel = (int)  node.getProperty (kInputBusHwChannel, -1);
    bus.stereo    = (bool) node.getProperty (kInputBusStereo, false);
    return bus;
}

//==============================================================================
// Reads
//==============================================================================

int InputBusManager::getNumBuses() const
{
    return readBusesTree().getNumChildren();
}

juce::Array<InputBusManager::InputBus> InputBusManager::getBuses() const
{
    juce::Array<InputBus> buses;
    const auto tree = readBusesTree();
    for (int i = 0; i < tree.getNumChildren(); ++i)
        buses.add (busFromNode (tree.getChild (i)));
    return buses;
}

InputBusManager::InputBus InputBusManager::getBus (int index) const
{
    const auto tree = readBusesTree();
    if (index < 0 || index >= tree.getNumChildren())
        return {};
    return busFromNode (tree.getChild (index));
}

int InputBusManager::indexOfBusNamed (const juce::String& name) const
{
    const auto tree = readBusesTree();
    for (int i = 0; i < tree.getNumChildren(); ++i)
        if (tree.getChild (i).getProperty (kInputBusName).toString() == name)
            return i;
    return -1;
}

int InputBusManager::indexOfBusForChannel (int hwChannel) const
{
    const auto tree = readBusesTree();
    for (int i = 0; i < tree.getNumChildren(); ++i)
        if ((int) tree.getChild (i).getProperty (kInputBusHwChannel, -1) == hwChannel)
            return i;
    return -1;
}

//==============================================================================
// CRUD — each begins its own undo transaction (I-02)
//==============================================================================

void InputBusManager::createBus (const juce::String& name, int hwChannel, bool stereo)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    auto& um = editManager.getUndoManager();
    um.beginNewTransaction ("Create Input Bus: " + name);

    auto tree = getOrCreateBusesTree();
    if (! tree.isValid())
        return;

    juce::ValueTree node (kInputBusNode);
    node.setProperty (kInputBusName,      name,      &um);
    node.setProperty (kInputBusRole,      juce::String(), &um);
    node.setProperty (kInputBusHwChannel, hwChannel, &um);
    node.setProperty (kInputBusStereo,    stereo,    &um);
    tree.appendChild (node, &um);

    // Push the new name down onto the matching WaveInputDevice alias (hybrid binding).
    applyToEngine();
}

void InputBusManager::renameBus (int index, const juce::String& newName)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    auto tree = getOrCreateBusesTree();
    if (index < 0 || index >= tree.getNumChildren())
        return;

    auto& um = editManager.getUndoManager();
    um.beginNewTransaction ("Rename Input Bus");
    tree.getChild (index).setProperty (kInputBusName, newName, &um);

    applyToEngine();
}

void InputBusManager::remapBus (int index, int hwChannel, bool stereo)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    auto tree = getOrCreateBusesTree();
    if (index < 0 || index >= tree.getNumChildren())
        return;

    auto& um = editManager.getUndoManager();
    um.beginNewTransaction ("Remap Input Bus");
    auto node = tree.getChild (index);
    node.setProperty (kInputBusHwChannel, hwChannel, &um);
    node.setProperty (kInputBusStereo,    stereo,    &um);

    applyToEngine();
}

void InputBusManager::deleteBus (int index)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    auto tree = getOrCreateBusesTree();
    if (index < 0 || index >= tree.getNumChildren())
        return;

    auto& um = editManager.getUndoManager();
    um.beginNewTransaction ("Delete Input Bus");
    tree.removeChild (index, &um);
}

//==============================================================================
// Engine binding
//==============================================================================

te::WaveInputDevice* InputBusManager::resolveBusToDevice (const InputBus& bus) const
{
    if (! bus.isValid())
        return nullptr;

    auto& dm = editManager.getEngine().getDeviceManager();

    te::WaveInputDevice* fallback = nullptr;  // first-channel match, stereo-ness differs

    // getWaveInputDevices() dispatches pending updates so the list is current even
    // before the async device scan runs (same rationale as TrackHeader).
    for (auto* dev : dm.getWaveInputDevices())
    {
        if (dev == nullptr)
            continue;

        const auto& channels = dev->getChannels();
        if (channels.empty() || channels.front().indexInDevice != bus.hwChannel)
            continue;

        if (dev->isStereoPair() == bus.stereo)
            return dev;          // exact match (channel + width)

        if (fallback == nullptr)
            fallback = dev;      // channel matches but mono/stereo differs
    }

    return fallback;
}

te::WaveInputDevice* InputBusManager::resolveBusToDevice (int index) const
{
    return resolveBusToDevice (getBus (index));
}

void InputBusManager::applyToEngine()
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    auto& dm = editManager.getEngine().getDeviceManager();

    // 1. Sync hardware channel pairing to the buses' stereo flags. Channels are
    //    paired (0-1, 2-3, …); a pair becomes a single stereo WaveInputDevice if
    //    ANY bus mapped to it is stereo. We only call setDeviceInChannelStereo()
    //    when a pair's state actually changes, so the common no-op apply (a rename,
    //    or startup with mono buses) never reconfigures the device list — avoiding
    //    a needless wave-device-list rebuild.
    if (auto* ad = dm.deviceManager.getCurrentAudioDevice())
    {
        const int numChans = ad->getInputChannelNames().size();
        const int numPairs = numChans / 2;

        std::vector<bool> wantStereo ((size_t) numPairs, false);
        for (const auto& bus : getBuses())
            if (bus.stereo && bus.hwChannel >= 0)
            {
                const int pair = bus.hwChannel / 2;
                if (pair < numPairs)
                    wantStereo[(size_t) pair] = true;
            }

        bool changed = false;
        for (int pair = 0; pair < numPairs; ++pair)
        {
            const int baseChan = pair * 2;
            if (dm.isDeviceInChannelStereo (baseChan) != wantStereo[(size_t) pair])
            {
                dm.setDeviceInChannelStereo (baseChan, wantStereo[(size_t) pair]);
                changed = true;
            }
        }

        if (changed)
            dm.dispatchPendingUpdates();  // flush the wave-device-list rebuild now
    }

    // 2. Push each bus name onto its matching WaveInputDevice alias — a cheap,
    //    persisted rename. Buses whose channel is absent (e.g. absorbed into a
    //    neighbouring stereo pair) resolve to nullptr and are skipped; the name
    //    stays in the session and the bus shows as offline in the picker.
    for (const auto& bus : getBuses())
        if (auto* dev = resolveBusToDevice (bus))
            dev->setAlias (bus.name);
}

void InputBusManager::initialiseDefaultBuses()
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    // Only seed when there are no buses yet — fresh session, or a Phase 1 session
    // saved before Named Buses existed. Never clobber user-defined buses.
    if (readBusesTree().getNumChildren() > 0)
        return;

    auto& dm = editManager.getEngine().getDeviceManager();
    auto tree = getOrCreateBusesTree();
    if (! tree.isValid())
        return;

    // Seeding is automatic migration, not a user action → nullptr UndoManager so it
    // does not pollute the undo history. The seeded buses still save with the session.
    for (auto* dev : dm.getWaveInputDevices())
    {
        if (dev == nullptr)
            continue;

        const auto& channels = dev->getChannels();
        const int hwChannel = channels.empty() ? -1 : channels.front().indexInDevice;
        if (hwChannel < 0)
            continue;

        const juce::String name = dev->getAlias().isNotEmpty() ? dev->getAlias()
                                                               : dev->getName();

        juce::ValueTree node (kInputBusNode);
        node.setProperty (kInputBusName,      name,                nullptr);
        node.setProperty (kInputBusRole,      juce::String(),      nullptr);
        node.setProperty (kInputBusHwChannel, hwChannel,           nullptr);
        node.setProperty (kInputBusStereo,    dev->isStereoPair(), nullptr);
        tree.appendChild (node, nullptr);
    }

    // Make sure the live device aliases match the seeded names.
    applyToEngine();
}
