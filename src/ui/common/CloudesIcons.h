#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * CloudesIcons — single source of truth for Cloudes' own vector-icon family
 * (Sketch 005, Pro-UI polish). Every icon is authored in a 24×24 box as a
 * juce::Path, so it never depends on font glyph coverage or source encoding
 * (the lesson from Fix 2's garbled ↶/↷). IconButton scales these to fit.
 *
 * Our own style — derived from Studio One's clarity, NOT copied: soft Apple-HIG
 * stroke, our blue/gold/teal palette applied by the button (not here).
 *
 * stroked(): true → draw with strokePath (outline icons: loop, tools, …),
 *            false → fillPath (solid icons: play/stop/record/pointer/rewind).
 */
enum class CloudesIcon
{
    Rewind, Play, Stop, Record, Loop, Metronome, Tap, Punch, Chevron,  // transport
    Pointer, Range, Scissors, Erase, Mute, Snap, Grid                  // tools
};

struct CloudesIcons
{
    static bool stroked (CloudesIcon ic) noexcept
    {
        switch (ic)
        {
            case CloudesIcon::Play:
            case CloudesIcon::Stop:
            case CloudesIcon::Record:
            case CloudesIcon::Rewind:
            case CloudesIcon::Pointer:
                return false;   // solid / filled
            default:
                return true;    // outline / stroked
        }
    }

    static juce::Path path (CloudesIcon ic)
    {
        juce::Path p;
        switch (ic)
        {
            case CloudesIcon::Play:
                p.addTriangle (7.0f, 5.0f, 19.0f, 12.0f, 7.0f, 19.0f);
                break;

            case CloudesIcon::Stop:
                p.addRoundedRectangle (7.0f, 7.0f, 10.0f, 10.0f, 1.6f);
                break;

            case CloudesIcon::Record:
                p.addEllipse (6.5f, 6.5f, 11.0f, 11.0f);
                break;

            case CloudesIcon::Rewind:                       // |◀  return to start
                p.addRectangle (5.0f, 6.0f, 2.4f, 12.0f);
                p.addTriangle (19.0f, 6.0f, 9.0f, 12.0f, 19.0f, 18.0f);
                break;

            case CloudesIcon::Loop:                         // circular repeat arrow
            {
                constexpr float pi = juce::MathConstants<float>::pi;
                p.addArc (4.5f, 5.0f, 15.0f, 14.0f, 0.55f * pi, 1.95f * pi, true);
                // arrowhead at the arc start (top-right area)
                const auto start = p.getCurrentPosition();
                juce::Path head;
                head.addTriangle (start.x - 3.5f, start.y - 1.0f,
                                  start.x + 1.0f, start.y - 4.0f,
                                  start.x + 1.5f, start.y + 2.5f);
                p.addPath (head);
                break;
            }

            case CloudesIcon::Metronome:                    // pendulum block + arm
                p.startNewSubPath (9.0f, 4.0f);
                p.lineTo (15.0f, 4.0f);
                p.lineTo (18.5f, 20.0f);
                p.lineTo (5.5f, 20.0f);
                p.closeSubPath();
                p.startNewSubPath (12.0f, 18.0f);
                p.lineTo (15.0f, 7.5f);
                break;

            case CloudesIcon::Tap:                          // ring + tick
                p.addEllipse (4.5f, 4.5f, 15.0f, 15.0f);
                p.startNewSubPath (12.0f, 12.0f);
                p.lineTo (12.0f, 7.0f);
                break;

            case CloudesIcon::Punch:                        // In/Out brackets + record dot (auto-punch region)
                p.startNewSubPath (7.0f, 5.0f);             // left bracket
                p.lineTo (4.0f, 5.0f);
                p.lineTo (4.0f, 19.0f);
                p.lineTo (7.0f, 19.0f);
                p.startNewSubPath (17.0f, 5.0f);            // right bracket
                p.lineTo (20.0f, 5.0f);
                p.lineTo (20.0f, 19.0f);
                p.lineTo (17.0f, 19.0f);
                p.addEllipse (9.5f, 9.5f, 5.0f, 5.0f);      // centre record dot (stroked ring)
                break;

            case CloudesIcon::Chevron:                      // small down caret (popover affordance)
                p.startNewSubPath (6.0f, 9.5f);
                p.lineTo (12.0f, 15.0f);
                p.lineTo (18.0f, 9.5f);
                break;

            case CloudesIcon::Pointer:                      // arrow cursor
                p.startNewSubPath (5.0f, 3.0f);
                p.lineTo (5.0f, 18.0f);
                p.lineTo (9.0f, 14.0f);
                p.lineTo (11.7f, 20.5f);
                p.lineTo (14.2f, 19.4f);
                p.lineTo (11.5f, 13.0f);
                p.lineTo (17.0f, 13.0f);
                p.closeSubPath();
                break;

            case CloudesIcon::Range:                        // selection rectangle
                p.addRoundedRectangle (4.0f, 7.0f, 16.0f, 10.0f, 1.5f);
                break;

            case CloudesIcon::Scissors:                     // two rings + blades
                p.addEllipse (3.5f, 4.0f, 5.0f, 5.0f);
                p.addEllipse (3.5f, 15.0f, 5.0f, 5.0f);
                p.startNewSubPath (8.0f, 7.5f);
                p.lineTo (20.0f, 16.5f);
                p.startNewSubPath (8.0f, 16.5f);
                p.lineTo (20.0f, 7.5f);
                break;

            case CloudesIcon::Erase:                        // eraser block + baseline
                p.startNewSubPath (3.5f, 14.5f);
                p.lineTo (11.0f, 7.0f);
                p.lineTo (17.5f, 13.5f);
                p.lineTo (12.5f, 18.5f);
                p.lineTo (8.5f, 18.5f);
                p.closeSubPath();
                p.startNewSubPath (8.5f, 18.5f);
                p.lineTo (20.0f, 18.5f);
                break;

            case CloudesIcon::Mute:                         // speaker + ✕
                p.startNewSubPath (4.0f, 9.5f);
                p.lineTo (4.0f, 14.5f);
                p.lineTo (8.5f, 14.5f);
                p.lineTo (13.0f, 18.5f);
                p.lineTo (13.0f, 5.5f);
                p.lineTo (8.5f, 9.5f);
                p.closeSubPath();
                p.startNewSubPath (16.5f, 9.5f);
                p.lineTo (20.5f, 14.5f);
                p.startNewSubPath (20.5f, 9.5f);
                p.lineTo (16.5f, 14.5f);
                break;

            case CloudesIcon::Snap:                         // magnet
                p.startNewSubPath (7.0f, 4.0f);
                p.lineTo (7.0f, 11.0f);
                p.addArc (7.0f, 6.0f, 10.0f, 10.0f, juce::MathConstants<float>::pi,
                          juce::MathConstants<float>::twoPi, false);
                p.lineTo (17.0f, 4.0f);
                p.startNewSubPath (5.0f, 4.0f);
                p.lineTo (9.0f, 4.0f);
                p.startNewSubPath (15.0f, 4.0f);
                p.lineTo (19.0f, 4.0f);
                break;

            case CloudesIcon::Grid:                         // hash
                p.startNewSubPath (4.0f, 9.0f);  p.lineTo (20.0f, 9.0f);
                p.startNewSubPath (4.0f, 15.0f); p.lineTo (20.0f, 15.0f);
                p.startNewSubPath (9.0f, 4.0f);  p.lineTo (9.0f, 20.0f);
                p.startNewSubPath (15.0f, 4.0f); p.lineTo (15.0f, 20.0f);
                break;
        }
        return p;
    }
};
