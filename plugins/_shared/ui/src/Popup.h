// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Something that opens over the window -- a Select's list, the CheckList's
 * panel -- and the rules for where it opens.
 *
 * INSIDE THE WINDOW, ALWAYS. "Every list and menu opens inside it, never past
 * its edge" (Spacing and layout): a plugin window has a fixed size and nothing
 * scrolls, so a list that escaped it would be cut off by the host's frame with
 * no way to reach the rest. A list is therefore a child of the window itself
 * -- the editor's root, which is the InfoHost every editor has -- laid out in
 * its coordinates and scaled with it, never a window of its own the way JUCE's
 * PopupMenu and ComboBox open one.
 *
 * WHERE: under what opened it, else above it, else wherever it fits; and a list
 * taller than the window is as tall as the window, and scrolls.
 *
 * A Popup is the layer a Select's list sits in: transparent, covering the
 * window, so a press anywhere outside the list closes it and goes no further
 * -- what the operating system's menu did for the web Select, whose list was
 * the OS's. (The CheckList's panel lets that press through, as the web one
 * did: CheckList.h.)
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace ni::ui
{

/* The window `c` is in: the nearest InfoHost above it -- an editor's root --
 * or, outside an editor (a test, the gallery), the top-level component. */
juce::Component& windowOf (juce::Component& c);

/*
 * Where a `width` x `height` box opens against `anchor`, both in the window's
 * coordinates: its left edge on the anchor's, `overlap` px up into the
 * anchor's bottom edge when below (or down into its top when above). Below if
 * it fits, else above, else as low as fits; moved left to stay inside; never
 * taller than the window.
 */
juce::Rectangle<int> placeInside (juce::Rectangle<int> window, juce::Rectangle<int> anchor,
                                  int width, int height, int overlap = 0);

class Popup final : public juce::Component
{
public:
    /* `dismissed` is called for a press outside the content. It may delete
     * this. */
    explicit Popup (std::function<void()> dismissed);
    ~Popup() override;

    /* Covers `window` and shows `content` at `bounds` (window coordinates)
     * on top of everything in it. */
    void show (juce::Component& window, juce::Component& content, juce::Rectangle<int> bounds);

    void mouseDown (const juce::MouseEvent&) override;

private:
    std::function<void()> dismissed;

    JUCE_DECLARE_NON_COPYABLE (Popup)
};

} // namespace ni::ui
