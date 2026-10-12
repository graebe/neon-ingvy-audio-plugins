// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * THE NODE ROW: sixteen 40px cells on the StepGrid's pitch, one a slot, and
 * the ISO cell at the well's right edge.
 *
 * A cell shows its slot's number and its type's glyph (16px, a 1.5px stroke,
 * square caps, as the system's icons): uv while the filter plays, ink-dim
 * while it is bypassed, nothing while the slot is empty. The selected cell's
 * border is uv with glow-focus. A click selects; Alt-click switches the
 * filter (or the isolator) off and on, as one gesture on its On.
 *
 * Each cell is a button to assistive technology, named by its slot and what
 * it holds. Message thread.
 */
#pragma once

#include "Model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace ni::fd
{

/* The glyph of a type (type::*), in a 16px box; empty for Empty. */
juce::Path typeGlyph (int type);

class NodeRow final : public juce::Component
{
public:
    static constexpr int cell = 40;
    static constexpr int pitch = 48;
    static constexpr int cells = param::nodes + 1;

    explicit NodeRow (Model&);
    ~NodeRow() override;

    void setSelected (int selection);
    std::function<void (int selection)> onSelect;

    /* The cells' look, read again from the model. */
    void update();

    juce::Button& cellAt (int index);

    void resized() override;

private:
    class Cell;

    Model& model;
    std::array<std::unique_ptr<Cell>, cells> row;
    int selection = 0;

    JUCE_DECLARE_NON_COPYABLE (NodeRow)
};

} // namespace ni::fd
