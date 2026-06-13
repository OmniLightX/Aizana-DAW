#include "MarkerLane.h"

//==============================================================================
namespace
{
    // Region / lane palette — amber accent reads clearly against the near-black ruler.
    const juce::Colour kLaneBg      { 0xff202020 };
    const juce::Colour kSeparator   { 0xff333333 };
    const juce::Colour kRegion      { 0xffd9a441 };  // amber body
    const juce::Colour kRegionEdge  { 0xfff0c25e };  // brighter amber for start/end edges
    const juce::Colour kRegionText  { 0xff1a1a1a };  // dark text on amber
    const juce::Colour kHint        { 0xff666666 };

    constexpr int    kTextPadX     = 6;     // text inset inside the region
    constexpr int    kResizeHandle = 6;     // px from the right edge that grabs to resize
    constexpr double kMinLenSecs   = 0.05;  // a marker region can't be dragged shorter than this
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

juce::Rectangle<int> MarkerLane::regionBounds (te::MarkerClip& clip) const
{
    const auto pos = clip.getPosition();
    const int x0 = timeToX (pos.getStart().inSeconds());
    const int x1 = timeToX (pos.getEnd().inSeconds());

    // Always at least grab-able even at extreme zoom-out.
    const int w = juce::jmax (kResizeHandle + 2, x1 - x0);
    return { x0, 1, w, getHeight() - 3 };
}

te::MarkerClip* MarkerLane::markerAtX (int x, bool& onRightEdge) const
{
    onRightEdge = false;

    auto* edit = getEdit();
    if (edit == nullptr) return nullptr;

    // Iterate in reverse so a later (visually on-top) region wins on overlap.
    auto markers = edit->getMarkerManager().getMarkers();
    for (int i = markers.size(); --i >= 0;)
    {
        if (auto* m = markers[i].get())
        {
            const auto b = regionBounds (*m);
            if (b.expanded (2, 0).contains (x, getHeight() / 2))
            {
                onRightEdge = (x >= b.getRight() - kResizeHandle);
                return m;
            }
        }
    }

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

        const auto b = regionBounds (*m);
        if (b.getRight() < 0 || b.getX() > getWidth())
            continue;

        // Region body (slightly translucent so overlapping sections still read).
        g.setColour (kRegion.withAlpha (0.85f));
        g.fillRect (b);

        // Brighter start anchor + right-edge resize grip.
        g.setColour (kRegionEdge);
        g.fillRect (b.getX(), b.getY(), 2, b.getHeight());
        g.fillRect (b.getRight() - 2, b.getY(), 2, b.getHeight());

        // Name, left-aligned, truncated to the region (clear of the resize grip).
        g.setColour (kRegionText);
        g.drawText (m->getName(),
                    b.reduced (kTextPadX, 0).withTrimmedRight (kResizeHandle),
                    juce::Justification::centredLeft, true);
    }
}

//==============================================================================
void MarkerLane::mouseDown (const juce::MouseEvent& e)
{
    auto* edit = getEdit();
    if (edit == nullptr) return;

    bool onRightEdge = false;
    auto* hit = markerAtX (e.x, onRightEdge);

    if (e.mods.isPopupMenu())
    {
        if (hit != nullptr)
            showMarkerContextMenu (hit->itemID);
        else
            createMarkerAt (xToTime (e.x));   // right-click empty = quick add
        return;
    }

    // Left button: arm a drag. Right edge → resize the region length, body → move.
    // A plain click (no drag) locates the transport on mouseUp.
    draggingMarker = hit;
    dragMode = hit == nullptr ? DragMode::None
                              : (onRightEdge ? DragMode::Resize : DragMode::Move);
    didDrag = false;
}

void MarkerLane::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingMarker == nullptr || dragMode == DragMode::None) return;

    auto* edit = getEdit();
    if (edit == nullptr) return;

    // I-02: one undo transaction per gesture — opened on the first drag delta,
    // subsequent set calls coalesce into it.
    if (! didDrag)
    {
        appState.editManager.getUndoManager().beginNewTransaction (
            dragMode == DragMode::Resize ? "Resize Marker" : "Move Marker");
        didDrag = true;
    }

    if (dragMode == DragMode::Resize)
    {
        const double startSecs = draggingMarker->getPosition().getStart().inSeconds();
        const double newLen = juce::jmax (kMinLenSecs, xToTime (e.x) - startSecs);
        draggingMarker->setLength (tracktion::TimeDuration::fromSeconds (newLen),
                                   /*preserveSync=*/ false);
    }
    else
    {
        draggingMarker->setStart (tracktion::TimePosition::fromSeconds (xToTime (e.x)),
                                  /*preserveSync=*/ false, /*keepLength=*/ true);
    }

    repaint();
}

void MarkerLane::mouseUp (const juce::MouseEvent&)
{
    // Plain click on a region (no drag) = locate transport to the marker start.
    if (draggingMarker != nullptr && ! didDrag)
    {
        if (auto* edit = getEdit())
            edit->getTransport().setPosition (draggingMarker->getPosition().getStart());
    }

    draggingMarker = nullptr;
    dragMode = DragMode::None;
    didDrag = false;
}

void MarkerLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;

    // Double-click on empty space creates a marker; on a region it does nothing
    // extra (the preceding single-click already located the transport).
    bool dummy = false;
    if (markerAtX (e.x, dummy) == nullptr)
        createMarkerAt (xToTime (e.x));
}

juce::MouseCursor MarkerLane::getMouseCursor()
{
    // Left-right resize cursor over a region's right edge signals "drag to stretch".
    bool onRightEdge = false;
    const auto p = getMouseXYRelative();
    if (markerAtX (p.x, onRightEdge) != nullptr && onRightEdge)
        return juce::MouseCursor (juce::MouseCursor::LeftRightResizeCursor);

    return juce::MouseCursor (juce::MouseCursor::NormalCursor);
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
