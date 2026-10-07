// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A dropdown that chooses SEVERAL: the web kit's CheckList.jsx, which the
 * Spectrogram's "view" uses to add channels to its picture.
 *
 * WHY NOT A SELECT. A select chooses one; turned into a multiple choice it
 * stops being a menu and selects with ctrl-click, a hostile thing to ask of
 * anyone in a plugin window. So this is the system's own answer: the Select's
 * face, and under it the Select card's open list -- "a bg-100 box with a uv
 * border" -- whose rows are switches, because a user-settable boolean is a
 * switch (Toggle) and the iconography rule forbids inventing a checkmark.
 *
 * THE FACE is a button in the Select's field: 28px, bg-200 on line-200, the
 * summary ("Kick, Bass", "Kick +3") cut with an ellipsis, the chevron, bg-300
 * under the pointer, a uv hairline while open, glow-focus for the keyboard.
 * Space, Enter or a click opens and closes it; Down or Up opens it and goes
 * to its first or last switch.
 *
 * THE PANEL (.checklist-panel): space-1 under the face, its right edge on the
 * face's, at least 160 wide and as wide as its widest row, at most 220 tall
 * and scrolling past that; padded space-2, rows space-2 apart, each row a
 * switch and its hint (a sample rate, "48k") apart, padded space-1, bg-300
 * under the pointer. A row that cannot be chosen -- a bus at another sample
 * rate -- is shown and refused, not hidden: the switch disabled, the hint
 * ink-dim, no hover. With nothing to list it says so (emptyText).
 *
 * INSIDE THE WINDOW (Popup.h): below the face, else above, else where it
 * fits. In it, Up and Down go from switch to switch, past the refused ones,
 * scrolling the panel as they go; Space or Enter flips one. A PRESS ANYWHERE
 * ELSE CLOSES IT AND STILL ARRIVES where it was aimed, as the web one's
 * document listener had it -- the next control can be used at once. Escape
 * closes it too, and the keyboard goes back to the face.
 *
 * IT CHOOSES NOTHING ITSELF: onChange is handed the ids that should be on,
 * and the owner sets them (setSelected) from what its model then holds.
 */
#pragma once

#include "Luminous.h"
#include "Pressable.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace ni::ui
{

class Toggle;

class CheckList : public juce::Component,
                  public Luminous
{
public:
    struct Option
    {
        int id = 0;
        juce::String name;
        juce::String hint;       // beside the name, in hint type ("48k")
        bool disabled = false;   // shown, and refused
    };

    CheckList();
    ~CheckList() override;

    void setOptions (std::vector<Option>);
    const std::vector<Option>& getOptions() const noexcept { return options; }

    /* The ids that are on. Sends nothing. */
    void setSelected (std::vector<int>);
    const std::vector<int>& getSelected() const noexcept { return selected; }
    bool isSelected (int id) const;

    /* What the face says: the caller's summary of the selection. */
    void setSummary (const juce::String&);
    /* What the panel says when there is nothing to list. */
    void setEmptyText (const juce::String&);
    /* A label beside the face, `width` wide, space-2 before it. */
    void setLabel (const juce::String&, int width = 44);
    /* The face's width: 116 by default. */
    void setFaceWidth (int);

    int idealWidth() const;
    juce::Rectangle<int> face() const;

    /* Moves the keyboard to the first switch that can be chosen from option
     * `from` on (-1: the last), going `direction` (1 or -1). Nothing while
     * closed. Returns the switch it chose, or nullptr for none. */
    Toggle* focusSwitch (int from, int direction);
    /* Option `id`'s place in the list, or -1. */
    int indexOf (int id) const;

    /* Asked for: the ids that should be on, in the order chosen. */
    std::function<void (std::vector<int>)> onChange;

    /* ---- the panel */
    void open();
    void close();
    void toggleOpen();
    bool isOpen() const noexcept { return panel != nullptr; }

    /* The open panel and its switches, or nullptr: for a test. */
    juce::Component* getPanel() const noexcept;
    Toggle* getSwitch (int id) const;

    /* A press anywhere in the window, as the window hears it: closes the
     * panel unless it was on the panel or the face. Public so a test can be
     * the window. */
    void pressedInWindow (juce::Component* target);

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;
    void enablementChanged() override;

private:
    class Face;
    class Panel;
    class Row;
    class Outside;

    void choose (int id, bool on);
    void refreshFace();
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    std::vector<Option> options;
    std::vector<int> selected;
    juce::String summary, emptyText { "nothing to list" }, label;
    int labelWidth = 44, faceWidth = 116;

    std::unique_ptr<Face> faceButton;
    std::unique_ptr<Panel> panel;
    std::unique_ptr<Outside> outside;
    juce::Component::SafePointer<juce::Component> window;

    JUCE_DECLARE_NON_COPYABLE (CheckList)
};

} // namespace ni::ui
