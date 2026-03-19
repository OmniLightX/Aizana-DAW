#pragma once

/**
 * EditTool — active mouse tool for the ArrangementView timeline area.
 *
 * Declared in a standalone header so both ArrangementView and ClipComponent
 * can include it without creating a circular dependency (ArrangementView.h
 * includes ClipComponent.h, so ClipComponent.h cannot include ArrangementView.h).
 */
enum class EditTool { Pointer, RangeSelect, Scissors };
