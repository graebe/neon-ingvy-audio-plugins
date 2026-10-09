// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The keyboard map. Keys.h has the conventions it implements.
 */
#include "Keys.h"

#include "Detents.h"

namespace ni::ui::keys
{

Key keyOf (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::leftKey)     return Key::left;
    if (code == juce::KeyPress::rightKey)    return Key::right;
    if (code == juce::KeyPress::upKey)       return Key::up;
    if (code == juce::KeyPress::downKey)     return Key::down;
    if (code == juce::KeyPress::homeKey)     return Key::home;
    if (code == juce::KeyPress::endKey)      return Key::end;
    if (code == juce::KeyPress::pageUpKey)   return Key::pageUp;
    if (code == juce::KeyPress::pageDownKey) return Key::pageDown;
    if (code == juce::KeyPress::returnKey)   return Key::enter;
    if (code == juce::KeyPress::escapeKey)   return Key::escape;
    return Key::none;
}

std::optional<int> gridMove (Key key, int index, int count, int cols)
{
    const int last = count - 1;
    switch (key)
    {
        case Key::right: return juce::jmin (last, index + 1);
        case Key::left:  return juce::jmax (0, index - 1);
        case Key::down:  return index + cols <= last ? index + cols : index;
        case Key::up:    return index - cols >= 0 ? index - cols : index;
        case Key::home:  return index - (index % cols);
        case Key::end:   return juce::jmin (last, index - (index % cols) + cols - 1);
        case Key::none:
        case Key::pageUp:
        case Key::pageDown:
        case Key::enter:
        case Key::escape:
            break;
    }
    return std::nullopt;
}

PadAction padKey (const juce::KeyPress& k, int index, int count, int cols)
{
    const auto key = keyOf (k);
    const auto mods = k.getModifiers();
    PadAction a;

    if (key == Key::enter)
    {
        a.kind = PadAction::Kind::toggle;
        a.tie = mods.isShiftDown();
        return a;
    }

    if (mods.isAltDown() && (key == Key::up || key == Key::down))
    {
        const float by = mods.isShiftDown() ? 0.01f : 0.1f;
        a.kind = PadAction::Kind::depth;
        a.depth = key == Key::up ? by : -by;
        return a;
    }

    if (const auto to = gridMove (key, index, count, cols))
    {
        a.kind = PadAction::Kind::move;
        a.to = *to;
    }
    return a;
}

SliderAction sliderKey (const juce::KeyPress& k, Axis axis)
{
    const float by = k.getModifiers().isShiftDown() ? fineStep : step;
    const bool x = axis != Axis::y;
    const bool y = axis != Axis::x;

    const auto delta = [] (float amount) { return SliderAction { SliderAction::Kind::delta, amount }; };
    const auto to = [] (float end) { return SliderAction { SliderAction::Kind::to, end }; };

    switch (keyOf (k))
    {
        case Key::right:    return x ? delta (by) : SliderAction {};
        case Key::left:     return x ? delta (-by) : SliderAction {};
        case Key::up:       return y ? delta (by) : SliderAction {};
        case Key::down:     return y ? delta (-by) : SliderAction {};
        case Key::pageUp:   return delta (by * 10.0f);
        case Key::pageDown: return delta (-by * 10.0f);
        case Key::home:     return to (0.0f);
        case Key::end:      return to (1.0f);
        case Key::none:
        case Key::enter:
        case Key::escape:
            break;
    }
    return {};
}

std::optional<int> tabMove (Key key, int index, int count)
{
    if (count <= 0)
        return std::nullopt;
    switch (key)
    {
        case Key::right:
        case Key::down: return (index + 1) % count;
        case Key::left:
        case Key::up:   return (index - 1 + count) % count;
        case Key::home: return 0;
        case Key::end:  return count - 1;
        case Key::none:
        case Key::pageUp:
        case Key::pageDown:
        case Key::enter:
        case Key::escape:
            break;
    }
    return std::nullopt;
}

std::optional<int> countKey (const juce::KeyPress& k, int value, int min, int max, int page,
                             const std::vector<double>& detentsAt)
{
    const auto clamp = [min, max] (int v) { return juce::jlimit (min, max, v); };
    const auto pageTo = [&] (int dir)
    {
        const auto next = detents::next ((double) value, detentsAt, dir);
        return clamp (next.has_value() ? juce::roundToInt (*next) : value + dir * page);
    };

    switch (keyOf (k))
    {
        case Key::up:
        case Key::right:    return clamp (value + 1);
        case Key::down:
        case Key::left:     return clamp (value - 1);
        case Key::pageUp:   return pageTo (1);
        case Key::pageDown: return pageTo (-1);
        case Key::home:     return min;
        case Key::end:      return max;
        case Key::none:
        case Key::enter:
        case Key::escape:
            break;
    }
    return std::nullopt;
}

} // namespace ni::ui::keys
