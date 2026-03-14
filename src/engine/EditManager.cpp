#include "EditManager.h"

// Convenience aliases for Tracktion's strong time types
using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;

//==============================================================================
EditManager::EditManager() = default;
EditManager::~EditManager() = default;

//==============================================================================
void EditManager::createNewEdit()
{
    // Create a fresh Edit with a single audio track.
    // The engine owns the Edit's UndoManager — all subsequent mutations route
    // through edit->getUndoManager() (the SINGLE shared undo manager per CONTEXT.md).
    edit = te::Edit::createSingleTrackEdit (engine);

    // Ensure transport is stopped (it is by default, but be explicit)
    edit->getTransport().stop (false, false);
}

//==============================================================================
void EditManager::loadEdit (const juce::File& file)
{
    // Use the Tracktion Engine helper that handles legacy format conversion.
    // loadEditFromFile() parses the XML, updates legacy state if needed, and
    // returns a fully constructed Edit ready for editing.
    edit = te::loadEditFromFile (engine, file, te::Edit::forEditing);

    if (edit == nullptr)
        jassertfalse; // File missing, invalid, or engine failed to construct Edit
}

//==============================================================================
void EditManager::saveEdit (const juce::File& file)
{
    if (edit == nullptr)
    {
        jassertfalse;
        return;
    }

    // Flush pending state changes into the ValueTree before serialising
    edit->flushState();

    // Use EditFileOperations for the canonical save path — handles the .tracktion format
    // that Tracktion Engine reloads correctly.
    te::EditFileOperations (*edit).saveAs (file, true);
}

//==============================================================================
void EditManager::beginAITransaction (const juce::String& description)
{
    if (edit == nullptr)
    {
        jassertfalse;
        return;
    }

    // All mutations between this call and the next beginNewTransaction() call
    // collapse into a single undo step — this is the AI batch undo requirement
    // from CONTEXT.md. Do NOT call beginNewTransaction() again during the batch.
    edit->getUndoManager().beginNewTransaction (description);
}

//==============================================================================
void EditManager::undoLastAction()
{
    if (edit != nullptr)
        edit->getUndoManager().undo();
}

void EditManager::redoLastAction()
{
    if (edit != nullptr)
        edit->getUndoManager().redo();
}

bool EditManager::canUndo() const
{
    return edit != nullptr && edit->getUndoManager().canUndo();
}

bool EditManager::canRedo() const
{
    return edit != nullptr && edit->getUndoManager().canRedo();
}

//==============================================================================
juce::UndoManager& EditManager::getUndoManager()
{
    jassert (edit != nullptr);
    return edit->getUndoManager();
}

//==============================================================================
void EditManager::moveClip (te::Clip& clip, double newStartTimeSecs)
{
    if (edit == nullptr)
    {
        jassertfalse;
        return;
    }

    // Each user moveClip begins a fresh transaction — creates one individual undo step.
    // This is the opposite of AI batch: 10 sequential moveClip() calls = 10 undo steps.
    edit->getUndoManager().beginNewTransaction ("Move clip");

    double lengthSecs = clip.getPosition().getLength().inSeconds();
    auto newStart = TimePos::fromSeconds (newStartTimeSecs);
    auto newEnd   = TimePos::fromSeconds (newStartTimeSecs + lengthSecs);
    clip.setPosition ({ TimeRng (newStart, newEnd), {} });
}

//==============================================================================
void EditManager::trimClip (te::Clip& clip, double newStartTimeSecs, double newEndTimeSecs)
{
    if (edit == nullptr)
    {
        jassertfalse;
        return;
    }

    // Each user trimClip begins a fresh transaction — creates one individual undo step.
    edit->getUndoManager().beginNewTransaction ("Trim clip");

    auto newStart = TimePos::fromSeconds (newStartTimeSecs);
    auto newEnd   = TimePos::fromSeconds (newEndTimeSecs);
    clip.setPosition ({ TimeRng (newStart, newEnd), {} });
}
