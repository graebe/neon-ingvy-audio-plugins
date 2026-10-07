// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A choice, not a quantity: the Select card, and the web kit's Select.jsx.
 *
 * THE FIELD: 28px, a bg-200 well on a line-200 hairline, the option in `value`
 * type padded 12px on the left and cut with an ellipsis rather than wrapped,
 * the design's chevron in ink-muted 6px in from the right. Under the pointer
 * the well rises to bg-300 and its hairline is ink-dim (1.1.0's States);
 * while its list is open its hairline is uv; keyboard
 * focus is glow-focus round it; disabled, bg-100 on line-100 in ink-dim. A
 * label may sit BESIDE it on the same 28px row, in the label style, a fixed
 * width wide (labelWidth): these say how a thing is measured or drawn, and a
 * panel of knobs leaves no room above.
 *
 * THE OPEN LIST, as the card has it: a bg-100 box with a uv hairline, one
 * 28px row per option padded 12px, the current option in uv ("selected is
 * uv"), the row under the pointer bg-300. As wide as the field and overlapping
 * its bottom hairline by one pixel, as .ph-select .list sits at top: 27px. It
 * opens INSIDE the window (Popup.h) -- below, else above, else where it fits --
 * where the web Select's list was the operating system's and could leave the
 * window, which 1.1.0 no longer allows.
 *
 * IT BEHAVES AS THE OS MENU DID:
 *
 *   press on the field      opens the list; release on a row without letting
 *                           go chooses it (press, drag, release), a click
 *                           leaves it open to choose from
 *   click a row             chooses it and closes
 *   press outside the list  closes it, and goes no further
 *   double-click the field  onReset: the parameter's default (1.1.0's
 *                           interaction conventions). The first click opens
 *                           the list, so the second lands on the list or the
 *                           layer round it, wherever it opened -- and is the
 *                           select's whenever it is over the field
 *   keys, closed            Space, Enter, Up or Down open it; typing chooses
 *   keys, open              Up and Down move, Home and End go to the ends,
 *                           Page Up and Down a list's height, Enter or Space
 *                           choose, Escape or Tab close; typing moves
 *
 * TYPING, as a focused <select> takes it: characters typed within typeAheadMs
 * of each other make one prefix, and the first option after the current one
 * that starts with it is chosen (closed) or moved to (open); one key pressed
 * again goes on to the next option that starts with it. On the Listen-In's
 * buses, 3 is bus 3 and 1, 6 is bus 16.
 *
 * The keyboard stays with the Select while its list is open, so its focus and
 * its ring never move; the list is only drawn and pointed at.
 *
 * IT TAKES AN INDEX, NOT A PARAMETER: what a choice means is the caller's
 * (ParamSelect binds a host parameter). onChange is told only of a choice
 * that is not the current one.
 *
 * NONE IS AN INDEX TOO: -1, for a choice the caller holds that no option
 * names (a channel whose bus has gone). The face shows the em dash a missing
 * reading shows, never a neighbouring option it would be mistaken for, and
 * every row is a change, so any pick is told.
 */
#pragma once

#include "Focus.h"
#include "Luminous.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace ni::ui
{

class Popup;

/*
 * The open list on its own: rows, the one under the pointer or the keys, the
 * current one in uv, and scrolling when the window is shorter than it.
 */
class SelectList final : public juce::Component
{
public:
    static constexpr int rowHeight = 28;

    SelectList (const juce::StringArray& options, int current);
    ~SelectList() override;

    /* The list's height for `count` rows: the rows and two hairlines. */
    static int heightFor (int count);

    int getNumRows() const noexcept { return options.size(); }
    int getCurrent() const noexcept { return current; }

    /* The row under the pointer or the keys (bg-300), or -1. */
    int getHighlighted() const noexcept { return highlighted; }
    void setHighlighted (int row);

    /* The row at a point, or -1; a row's place, in the list's coordinates. */
    int rowAt (juce::Point<int>) const;
    juce::Rectangle<int> rowBounds (int row) const;

    /* The first row shown, when they do not all fit. */
    int getFirstShown() const noexcept { return first; }
    int numShown() const;

    /* The keys of an open list. True if it was one. */
    bool handleKey (const juce::KeyPress&);

    /* A row was chosen: by a click, or Enter or Space on the highlighted one.
     * The list may be deleted from it. */
    std::function<void (int row)> onChoose;

    /* A press on the list, before it is a choice: true if the owner took it
     * (and the list may be gone). */
    std::function<bool (const juce::MouseEvent&)> onPress;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void scrollToShow (int row);
    void scrollTo (int firstRow);
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    juce::StringArray options;
    int current = 0;
    int highlighted = -1;
    int first = 0;
    float wheel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE (SelectList)
};

class Select : public juce::Component,
               public Luminous
{
public:
    Select();
    ~Select() override;

    /* The options, in order, and the one shown: an option's index, or -1 for
     * none (any negative is none). setIndex sends nothing. */
    void setOptions (const juce::StringArray&);
    const juce::StringArray& getOptions() const noexcept { return options; }
    void setIndex (int);
    int getIndex() const noexcept { return index; }

    /* A label beside the field, `width` wide (44 by default), space-2 before
     * it; empty for none. It is also the title a screen reader hears, unless
     * the select is given another (setTitle: "Slot"). */
    void setLabel (const juce::String&, int width = 44);

    /* The field's width: 96 by default (.select { min-width }), whatever the
     * component's. */
    void setFieldWidth (int);

    /* The label, its gap and the field. Height is control-h. */
    int idealWidth() const;

    /* The field, in the select's coordinates. */
    juce::Rectangle<int> field() const;

    /* A choice of another option. */
    std::function<void (int index)> onChange;
    /* A double-click on the field: back to the default, which only the
     * owner knows. */
    std::function<void()> onReset;

    /* How long typed characters run together into one prefix. */
    static constexpr double typeAheadMs = 1000.0;

    /* ---- the list */
    void open();
    void close();
    bool isOpen() const noexcept { return list != nullptr; }
    /* The open list, or nullptr. */
    SelectList* getList() const noexcept { return list.get(); }

    bool isFieldHovered() const noexcept { return fieldHovered; }

    /* ---- juce::Component */
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    void enablementChanged() override;

    /* ---- Luminous: glow-focus round the field */
    void paintLight (juce::Graphics&) override;

private:
    void choose (int);
    bool secondPress (const juce::MouseEvent&);
    /* The option a typed key goes to; -1 for typing that names none, -2
     * for a key that is not typing. */
    int typedOption (const juce::KeyPress&);
    void setFieldHovered (bool);
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    juce::StringArray options;
    int index = 0;
    juce::String label;
    int labelWidth = 44;
    int fieldWidth = 96;
    bool fieldHovered = false;
    bool pressOpened = false;
    juce::String typed;
    double typedAt = 0.0;

    FocusVisibility focus { *this };
    std::unique_ptr<SelectList> list;
    std::unique_ptr<Popup> popup;

    JUCE_DECLARE_NON_COPYABLE (Select)
};

} // namespace ni::ui
