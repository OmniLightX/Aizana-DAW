#include "TrackOperations.h"
#include "MixerEngine.h"

namespace te = tracktion::engine;

namespace aizana::TrackOperations
{
bool canDuplicateTrack (const te::AudioTrack& track)
{
    // Unknown channel tags are deliberately excluded as well as bus/FX/VCA/cue.
    // Aux-return identities must never be duplicated into another receiving track.
    if (! MixerEngine::isAudioChannel (track))
        return false;
    for (auto* plugin : track.pluginList)
        if (plugin->getPluginType() == "auxreturn")
            return false;
    return true;
}

te::AudioTrack::Ptr duplicateTrack (te::AudioTrack& source)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (! canDuplicateTrack (source))
        return {};

    te::AudioTrack::Ptr destination = source.getOutput().getDestinationTrack();
    auto& undo = source.edit.getUndoManager();
    undo.beginNewTransaction ("Spur duplizieren");
    // TE flushes hosted plugin state, remaps every declared edit-item ID and
    // reconnects local modifier assignments. External routing IDs stay intact.
    auto copied = source.edit.copyTrack (&source, te::TrackInsertPoint (source, false));
    te::AudioTrack::Ptr result = dynamic_cast<te::AudioTrack*> (copied.get());
    if (result != nullptr)
    {
        // TE serialises destinations as track numbers. Inserting the copy can
        // shift that number, so restore the actual destination object explicitly.
        if (destination != nullptr)
            result->getOutput().setOutputToTrack (destination.get());
        result->state.setProperty (te::IDs::name, source.getName() + " Kopie", &undo);
        result->state.setProperty ("armed", false, &undo);
        // Freeze caches belong to the old track ID; the copy plays its own chain.
        result->state.setProperty (te::IDs::frozen, false, &undo);
        result->state.setProperty (te::IDs::frozenIndividually, false, &undo);
        result->setSoloIsolate (false);
    }
    undo.beginNewTransaction();
    return result;
}

bool moveTrackRelative (te::AudioTrack& source, te::AudioTrack& target, bool after)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (&source == &target || &source.edit != &target.edit
        || ! canDuplicateTrack (source) || ! canDuplicateTrack (target))
        return false;

    auto parent = source.state.getParent();
    if (! parent.isValid() || parent != target.state.getParent())
        return false;
    const int oldIndex = parent.indexOf (source.state);
    const int targetIndex = parent.indexOf (target.state);
    const int newIndex = targetIndex + (after ? 1 : 0) - (oldIndex < targetIndex ? 1 : 0);
    if (oldIndex == newIndex)
        return false;

    auto& undo = source.edit.getUndoManager();
    undo.beginNewTransaction ("Spur verschieben");
    parent.moveChild (oldIndex, newIndex, &undo);
    undo.beginNewTransaction();
    return true;
}
}
