// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Tabs: the web kit's Tabs.jsx -- a vertical strip of index cards, one lit,
 * as the Trance Gate lays one over the right edge of its plot band.
 *
 * NOT A SYSTEM CARD: Ultraviolet has no Tabs, so nothing here is invented.
 * A tab is a button that is exclusive and turned on its side, in the Button
 * card's colours: a bg-200 well on a line-200 hairline, bg-300 under the
 * pointer, and the lit one a uv fill with on-uv text and glow-led. The text
 * is the `hint` style in capitals, ink-muted, reading bottom to top -- how a
 * tab on a right edge is read -- and the strip is 24px wide, each tab an equal
 * share of its height.
 *
 * THE LIGHT IS PAINTED AS CSS PAINTS IT, in document order: a lit or focused
 * tab's halo falls over the tabs above it and under the ones below it, which
 * paint after it. Past the strip it is the parent's (Luminous).
 *
 * THE KEYBOARD, as a tab list has it: one Tab stop, the lit tab; the arrows
 * (Up and Down, and Left and Right) move to the next tab and choose it,
 * wrapping; Home and End go to the ends; Space or Enter chooses the tab with
 * the focus. Each tab says what it shows in its own info line.
 *
 * IT DOES NOT CHOOSE BY ITSELF: onSelect asks, and the owner says which is
 * lit with setActive.
 */
#pragma once

#include "Pressable.h"

#include <functional>
#include <vector>

namespace ni::ui
{

class Tabs : public juce::Component,
             public Luminous,
             public PressableParent
{
public:
    /* The strip's width. */
    static constexpr int width = 24;

    Tabs();
    ~Tabs() override;

    /* The tabs, top to bottom, and each one's info line (or none). */
    void setTabs (const juce::StringArray& names, const juce::StringArray& infos = {});
    int size() const noexcept { return tabs.size(); }

    /* Which is lit. Sends nothing. */
    void setActive (int index);
    int getActive() const noexcept { return active; }

    /* Asked for: the tab that should be lit. */
    std::function<void (int index)> onSelect;

    /* A tab, as a component -- for a test, or for a window that puts it in
     * its focus order. */
    Pressable& getTab (int index) const;

    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;
    void pressableStateChanged (Pressable&) override;

private:
    class Tab;

    void choose (int index, bool fromKeys);
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    juce::OwnedArray<Tab> tabs;
    int active = 0;

    JUCE_DECLARE_NON_COPYABLE (Tabs)
};

} // namespace ni::ui
