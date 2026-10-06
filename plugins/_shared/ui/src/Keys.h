// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What a key does to a control, as plain functions -- the web kit's
 * lib/keys.js, for JUCE.
 *
 * "A focus ring on a control you cannot operate is decoration." Every control
 * that takes the pointer takes the keyboard too, and the mapping lives here so
 * every control answers the same keys the same way, and so it can be tested
 * without a component. From the system's Interaction conventions:
 *
 *   arrows        step: 1 % of the range; Shift fine, 0.2 % (the drag's ratio of 5)
 *   Page Up/Down  a large step, 10 % -- or the next detent, where there is one
 *   Home / End    the ends of the range
 *   Enter         types into a knob's readout
 *   Space, Enter  press a button or a switch
 *   a step grid   one Tab stop: arrows move between steps, Space or Enter
 *                 toggles (Shift: a tie), Alt with Up or Down sets the amount
 *
 * Every keystroke is an edit of its own: one gesture per key (ParamBinding's
 * commit), so a host records one automation write per press rather than a
 * touch that never ends.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

namespace ni::ui::keys
{

/* A step, a fine step and a page, as shares of a range. */
inline constexpr float step = 0.01f;
inline constexpr float fineStep = 0.002f;
inline constexpr float pageStep = 0.1f;

/* The keys a control answers, by what they mean rather than their code. */
enum class Key
{
    none,
    left, right, up, down,
    home, end, pageUp, pageDown,
    space, enter, escape,
};

Key keyOf (const juce::KeyPress&);

/*
 * A grid of `count` cells, `cols` to a row: where an arrow, Home or End moves
 * focus from `index`, or nothing for a key that is not a move. Does not wrap:
 * the edges stop, as a grid in a window does.
 */
std::optional<int> gridMove (Key, int index, int count, int cols);

/* A step pad's keys: Space or Enter toggles (Shift: a tie); with Alt held the
 * vertical arrows set its amount (Shift: finer); plain arrows move. */
struct PadAction
{
    enum class Kind { none, toggle, depth, move };

    Kind kind = Kind::none;
    bool tie = false;       // toggle: Shift was held
    float depth = 0.0f;     // depth: the change of amount
    int to = -1;            // move: the pad to go to
};
PadAction padKey (const juce::KeyPress&, int index, int count, int cols);

/* A slider's keys, as a share of its range: arrows step (Shift: fine), Page
 * pages, Home and End go to 0 and 1. `axis` limits the arrows to Left/Right
 * or Up/Down. */
enum class Axis { x, y, both };

struct SliderAction
{
    enum class Kind { none, delta, to };

    Kind kind = Kind::none;
    float amount = 0.0f;    // delta: the change; to: 0 or 1
};
SliderAction sliderKey (const juce::KeyPress&, Axis axis = Axis::both);

/* Tabs: the arrows move, wrapping; Home and End go to the ends. */
std::optional<int> tabMove (Key, int index, int count);

/*
 * A count -- a length in steps, say: arrows by one, Page by `page`, Home and
 * End to the ends. With `detents`, Page goes to the next one that way
 * instead, and by `page` only where there is none.
 */
std::optional<int> countKey (const juce::KeyPress&, int value, int min, int max, int page = 4,
                             const std::vector<double>& detents = {});

} // namespace ni::ui::keys
