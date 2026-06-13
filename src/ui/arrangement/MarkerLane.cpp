#include "MarkerLane.h"

//==============================================================================
namespace
{
    // Flag / lane palette — amber accent reads clearly against the near-black ruler.
    const juce::Colour kLaneBg     { 0xff202020 };
    const juce::Colour kSeparator  { 0xff333333 };
    const juce::Colour kFlag       { 0xffd9a441 };  // amber
    const juce::Colour kFlagText   { 0xff1a1a1a };  // dark text on amber tab
    const juce::Colour kHint       { 0xff666666 };

    constexpr int kFlagPadX = 6;   // horizontal text padding inside a flag tab
    constexpr int kMinFlagW = 14;
}

//==============================================================================
MarkerLane::MarkerLane (ApplicationState& appState_,
                        double& pixelsPerSecond_,
                        double& scrollPositionSecs_)
    : appState           (appState_)
    , pixelsPerSecond    (pixelsPerSecond_)
    , scrollPositionSecs (scrollPositionSecs_)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

//==============================================================================
te::Edit* MarkerLane::getEdit() const
{
    return appState.editManager.getEdit();
}

double MarkerLane::xToTime (int x) const
{
    return juce::jmax (0.0, scrollPositionSecs + (double) x / pixelsPerSecond);
}

int MarkerLane::timeToX (double sec) const
{
    return (int) ((sec - scrollPositionSecs) * pixelsPerSecond);
}

juce::Rectangle<int> MarkerLane::flagBounds (te::MarkerClip& clip) const
{
    const int x = timeToX (clip.getPosition().getStart().inSeconds());

    const juce::Font font (juce::FontOptions (11.0f));
    const int textW = juce::GlyphArrangement::getStringWidthInt (font, clip.getName());
    const int w = juce::jmax (kMinFlagW, textW + kFlagPadX * 2);

    return { x, 1, w, getHeight() - 3 };
}

te::MarkerClip* MarkerLane::markerAtX (int x) const
{
    auto* edit = getEdit();
    if (edit == nullptr) return nullptr;

    // Iterate in reverse so a later (visually on-top) flag wins on overlap.
    auto markers = edit->getMarkerManager().getMarkers();
    for (int i = markers.size(); --i >= 0;)
        if (auto* m = markers[i].get())
            if (flagBounds (*m).expanded (3, 0).contains (x, getHeight() / 2))
                return m;

    return nullptr;
}

te::MarkerClip* MarkerLane::findMarker (te::EditItemID id) const
{
    auto* edit = getEdit();
    if (edit == nullptr) return nullptr;

    // ReferenceCountedArray range-iteration yields raw MarkerClip pointers.
    auto markers = edit->getMarkerManager().getMarkers();
    for (auto* m : markers)
        if (m != nullptr && m->itemID == id)
            return m;

    return nullptr;
}

//==============================================================================
void MarkerLane::paint (juce::Graphics& g)
{
    g.fillAll (kLaneBg);

    g.setColour (kSeparator);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    auto* edit = getEdit();
    if (edit == nullptr) return;

    g.setFont (juce::FontOptions (11.0f));

    auto markers = edit->getMarkerManager().getMarkers();

    if (markers.isEmpty())
    {
        // Discoverability hint when the lane is empty.
        g.setColour (kHint);
        g.drawText (juce::String::fromUTF8 ("Doppelklick = Marker setzen"),
                    8, 0, getWidth() - 16, getHeight(),
                    juce::Justification::centredLeft, true);
        return;
    }

    for (auto& m : markers)
    {
        if (m == nullptr) continue;

        const auto bounds = flagBounds (*m);
        if (bounds.getRight() < 0 || bounds.getX() > getWidth())
            continue;

        // Position anchor — thin vertical line at the exact marker time.
        g.setColour (kFlag);
        g.drawVerticalLine (bounds.getX(), 0.0f, (float) getHeight());

        // Flag tab + name.
        g.setColour (kFlag);
        g.fillRect (bounds);
        g.setColour (kFlagText);
        g.drawText (m->getName(), bounds.reduced (kFlagPadX, 0),
                    juce::Justification::centredLeft, true);
    }
}

//==============================================================================
void MarkerLane::mouseDown (const juce::MouseEvent& e)
{
    auto* edit = getEdit();
    if (edit == nullptr) return;

    auto* hit = markerAtX (e.x);

    if (e.mods.isPopupMenu())
    {
        if (hit != nullptr)
            showMarkerContextMenu (hit->itemID);
        else
            createMarkerAt (xToTime (e.x));   // right-click empty = quick add
        return;
    }

    // Left button: prepare a potential drag on a flag; seek happens on mouseUp
    // only if no drag occurred (so a plain click locates without moving).
    draggingMarker = hit;
    didDrag = false;
}

void MarkerLane::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingMarker == nullptr) return;

    auto* edit = getEdit();
    if (edit == nullptr) return;

    // I-02: one undo transaction per gesture — opened on the first drag delta,
    // subsequent setStart calls coalesce into it.
    if (! didDrag)
    {
        appState.editManager.getUndoManager().beginNewTransaction ("Move Marker");
        didDrag = true;
    }

    const double newTime = xToTime (e.x);
    draggingMarker->setStart (tracktion::TimePosition::fromSeconds (newTime),
                              /*preserveSync=*/ false, /*keepLength=*/ true);
    repaint();
}

void MarkerLane::mouseUp (const juce::MouseEvent&)
{
    // Plain click on a flag (no drag) = locate transport to the marker.
    if (draggingMarker != nullptr && ! didDrag)
    {
        if (auto* edit = getEdit())
            edit->getTransport().setPosition (draggingMarker->getPosition().getStart());
    }

    draggingMarker = nullptr;
    didDrag = false;
}

void MarkerLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;

    // Double-click on empty space creates a marker; on a flag it does nothing
    // extra (the preceding single-click already located the transport).
    if (markerAtX (e.x) == nullptr)
        createMarkerAt (xToTime (e.x));
}

//==============================================================================
void MarkerLane::addMarkerAtPlayhead()
{
    if (auto* edit = getEdit())
        createMarkerAt (edit->getTransport().getPosition().inSeconds());
}

void MarkerLane::createMarkerAt (double timeSecs)
{
    auto* edit = getEdit();
    if (edit == nullptr) return;

    // I-02: discrete user action = one undo transaction.
    appState.editManager.getUndoManager().beginNewTransaction ("Add Marker");

    const auto pos = tracktion::TimePosition::fromSeconds (juce::jmax (0.0, timeSecs));
    if (auto clip = edit->getMarkerManager().createMarker (-1, pos,
                                                           tracktion::TimeDuration(), nullptr))
    {
        // createMarker returns a Clip::Ptr — getMarkerID lives on MarkerClip.
        if (auto* mc = dynamic_cast<te::MarkerClip*> (clip.get()))
            mc->setName ("Marker " + juce::String (mc->getMarkerID()));   // unique auto-number
    }

    repaint();
}

void MarkerLane::showMarkerContextMenu (te::EditItemID id)
{
    juce::PopupMenu m;
    m.addItem (1, juce::String::fromUTF8 ("Umbenennen…"));
    m.addItem (2, juce::String::fromUTF8 ("Löschen"));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [safe = juce::Component::SafePointer<MarkerLane> (this), id] (int choice)
        {
            if (safe == nullptr) return;            // I-10: nullptr-guard first
            if (choice == 1) safe->promptRename (id);
            else if (choice == 2) safe->deleteMarker (id);
        });
}

void MarkerLane::promptRename (te::EditItemID id)
{
    auto* clip = findMarker (id);
    if (clip == nullptr) return;

    auto* aw = new juce::AlertWindow (juce::String::fromUTF8 ("Marker umbenennen"),
                                      "Name:", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", clip->getName());
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton (juce::String::fromUTF8 ("Abbrechen"), 0,
                   juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [aw, safe = juce::Component::SafePointer<MarkerLane> (this), id] (int result)
            {
                if (result != 1 || safe == nullptr) return;   // I-10: guard first

                const auto newName = aw->getTextEditorContents ("name").trim();
                auto* m = safe->findMarker (id);              // re-resolve, never hold raw ptr
                if (m == nullptr || newName.isEmpty()) return;

                safe->appState.editManager.getUndoManager().beginNewTransaction ("Rename Marker");
                m->setName (newName);
                safe->repaint();
            }),
        true);   // deleteWhenDismissed — aw outlives the callback, then self-deletes
}

void MarkerLane::deleteMarker (te::EditItemID id)
{
    auto* clip = findMarker (id);
    if (clip == nullptr) return;

    appState.editManager.getUndoManager().beginNewTransaction ("Delete Marker");
    clip->removeFromParent();
    repaint();
}
