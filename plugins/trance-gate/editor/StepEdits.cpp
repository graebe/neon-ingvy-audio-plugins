// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Editing a step. StepEdits.h has the rules.
 */
#include "StepEdits.h"

namespace ni::tg
{

StepEdits::StepEdits (Model& m) : model (m) {}

StepMode StepEdits::modeOf (int index) const
{
    const auto& p = model.pattern();
    return index >= 0 && index < maxSteps ? p.steps[(std::size_t) index] : StepMode::off;
}

bool StepEdits::fadeOut() const
{
    return model.parameter (param::fadeDir).getValue() >= 0.5f;
}

bool StepEdits::arriving (int index) const
{
    return fadeOut() == (modeOf (index) == StepMode::off);
}

int StepEdits::arrivals() const
{
    const int length = juce::jlimit (1, maxSteps, model.pattern().length);
    const bool holes = fadeOut();
    const auto& steps = model.pattern().steps;
    int n = 0;
    for (int i = 0; i < length; ++i)
        n += holes == (steps[(std::size_t) i] == StepMode::off) ? 1 : 0;
    return n;
}

bool StepEdits::isNamed (int index) const
{
    return index >= 0 && index < maxSteps && namedSteps[(std::size_t) index];
}

void StepEdits::setOrdering (bool on)
{
    orderMode = on;
    namedSteps.fill (false);
    named = 0;
}

StepMode StepEdits::cycle (int index, bool tie)
{
    const auto was = modeOf (index);
    const bool on = was != StepMode::off;
    const auto mode = tie ? (was == StepMode::on ? StepMode::tie : StepMode::on)
                          : (on ? StepMode::off : StepMode::on);
    model.setStep (index, mode);
    if (! on && mode != StepMode::off)
        model.setDepth (index, 1.0f);
    return mode;
}

bool StepEdits::name (int index)
{
    if (! orderMode)
        return false;
    if (index < 0 || index >= maxSteps || ! arriving (index) || namedSteps[(std::size_t) index])
        return true;
    model.setOrder (index, named + 1);
    namedSteps[(std::size_t) index] = true;
    ++named;
    return true;
}

bool StepEdits::press (int index, bool shift)
{
    if (name (index))
        return false;
    pressed = cycle (index, shift);
    dragOn = pressed != StepMode::off;
    return true;
}

void StepEdits::drag (int index, float amount)
{
    if (amount <= offAmount)
    {
        if (dragOn)
            model.setStep (index, StepMode::off);
        dragOn = false;
        return;
    }
    if (! dragOn)
    {
        model.setStep (index, StepMode::on);
        dragOn = true;
    }
    model.setDepth (index, juce::jlimit (0.0f, 1.0f, amount));
}

void StepEdits::sweep (int index)
{
    if (orderMode)
        return;
    const bool wasOff = modeOf (index) == StepMode::off;
    model.setStep (index, pressed);
    if (pressed != StepMode::off && wasOff)
        model.setDepth (index, 1.0f);
}

void StepEdits::toggle (int index, bool tie)
{
    if (! name (index))
        cycle (index, tie);
}

void StepEdits::nudge (int index, float delta)
{
    if (index < 0 || index >= maxSteps)
        return;
    const float amount = juce::jlimit (0.0f, 1.0f, model.pattern().depths[(std::size_t) index] + delta);
    if (amount <= offAmount)
    {
        model.setStep (index, StepMode::off);
        return;
    }
    if (modeOf (index) == StepMode::off)
        model.setStep (index, StepMode::on);
    model.setDepth (index, amount);
}

void StepEdits::typeRank (int index, const juce::String& typed)
{
    const auto text = typed.trim();
    if (! text.containsOnly ("0123456789") || text.isEmpty())
        return;
    const int rank = text.getIntValue();
    if (rank >= 1)
        model.setOrder (index, rank);
}

void StepEdits::shuffle()
{
    model.shuffleOrder();
}

} // namespace ni::tg
