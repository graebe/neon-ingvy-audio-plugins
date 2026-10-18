// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The tool column. ToolColumn.h has what it shows and how one is picked.
 */
#include "ToolColumn.h"

#include "ChildLights.h"
#include "Info.h"

namespace ni::ui
{

namespace
{
constexpr int controlH = (int) uv::tok::size::controlH;
}

ToolColumn::ToolColumn()
{
    setTitle ("Tools");
}

ToolColumn::~ToolColumn() = default;

void ToolColumn::setTools (const std::vector<Tool>& tools)
{
    buttons.clear();
    for (size_t i = 0; i < tools.size(); ++i)
    {
        auto b = std::make_unique<Button> (juce::String(), tools[i].glyph);
        b->setTitle (tools[i].name);
        setInfo (*b, tools[i].info);
        b->setExplicitFocusOrder ((int) i + 1);
        b->onClick = [this, index = (int) i]
        {
            if (index == selected)
                return;
            setSelected (index);
            if (onChange)
                onChange (index);
        };
        addAndMakeVisible (*b);
        buttons.push_back (std::move (b));
    }
    selected = 0;
    for (size_t i = 0; i < buttons.size(); ++i)
        buttons[i]->setOn (i == 0);
    resized();
}

void ToolColumn::setSelected (int index)
{
    if (index < 0 || index >= size())
        return;
    selected = index;
    for (int i = 0; i < size(); ++i)
        button (i).setOn (i == index);
}

juce::Rectangle<int> ToolColumn::idealSize() const
{
    const int n = (int) buttons.size();
    return { controlH, n == 0 ? 0 : n * controlH + (n - 1) * gap };
}

void ToolColumn::resized()
{
    const int x = (getWidth() - controlH) / 2;
    int y = 0;
    for (auto& b : buttons)
    {
        b->setBounds (x, y, controlH, controlH);
        y += controlH + gap;
    }
}

void ToolColumn::paint (juce::Graphics& g)
{
    /* No ground of its own: the lit tool's light, in the gaps. */
    paintChildLights (g, *this);
}

void ToolColumn::paintLight (juce::Graphics& g)
{
    forwardChildLights (g, *this);
}

std::unique_ptr<juce::AccessibilityHandler> ToolColumn::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}

} // namespace ni::ui
