// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The tools of an editor that has them -- what the pointer does in its well --
 * as one column of 28px icon-only buttons (the Button card's), space-3 (12px)
 * apart, exactly one of them on.
 *
 * ONE IS ALWAYS ON. A click on a tool picks it and lights it (an icon-only
 * button that is on glows like a step, Button.h); a click on the one already
 * on changes nothing. The column says which through onChange, and the editor
 * says what the tool means -- the hint bar's conventions, the pointer in the
 * well. setSelected() picks one without telling anyone, for the editor that
 * already knows.
 *
 * EACH TOOL IS ITS NAME, ITS GLYPH AND ITS LINE: the name is the button's
 * accessible title (an icon replaces the word), the glyph is the system's or
 * the product's own (UvIcons.h), and the line is its info line. To a screen
 * reader each is a toggle, on or off, in a group titled as the column is.
 *
 * The buttons sit centred across the column's width, from its top; give it
 * at least idealSize(). Their light reaches past it, so the column is
 * Luminous and passes it on (ChildLights.h).
 */
#pragma once

#include "Button.h"
#include "UvTokens.h"

#include <functional>
#include <memory>
#include <vector>

namespace ni::ui
{

class ToolColumn : public juce::Component,
                   public Luminous
{
public:
    /* .tools { gap: var(--s3) } */
    static constexpr int gap = (int) uv::tok::space::space3;

    struct Tool
    {
        juce::String name;
        juce::String glyph;
        juce::String info;
    };

    ToolColumn();
    ~ToolColumn() override;

    /* The tools, top to bottom; the first is on. */
    void setTools (const std::vector<Tool>&);
    int size() const noexcept { return (int) buttons.size(); }

    /* The tool that is on, and picking one: silently, for the caller that
     * already knows. Out of range is ignored. */
    int getSelected() const noexcept { return selected; }
    void setSelected (int index);

    /* A tool was picked by a click, or a screen reader's press. */
    std::function<void (int index)> onChange;

    Button& button (int index) noexcept { return *buttons[(size_t) index]; }

    /* The buttons' column: control-h wide, n of them and the gaps between. */
    juce::Rectangle<int> idealSize() const;

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    std::vector<std::unique_ptr<Button>> buttons;
    int selected = 0;

    JUCE_DECLARE_NON_COPYABLE (ToolColumn)
};

} // namespace ni::ui
