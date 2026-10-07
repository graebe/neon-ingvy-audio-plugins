// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The pads. Pads.h has what they show and why.
 */
#include "Pads.h"

#include "InfoLines.h"

#include "ChildLights.h"
#include "Info.h"
#include "UvType.h"

namespace ni::tg
{

namespace
{
ni::ui::Step::Drawn drawnOf (StepMode m)
{
    switch (m)
    {
        case StepMode::on:  return ni::ui::Step::Drawn::on;
        case StepMode::tie: return ni::ui::Step::Drawn::tie;
        case StepMode::off: break;
    }
    return ni::ui::Step::Drawn::off;
}

/* Where a Step draws its number, and the box a press on it lands in: the
 * glyphs and a few pixels round them, inside the pad's top-left corner. */
juce::Rectangle<int> numberBox (juce::Rectangle<int> step, int number)
{
    const float w = uv::type::width (uv::type::hint(), juce::String (number));
    return { step.getX() + 1, step.getY() + 1, juce::roundToInt (w) + 7, Pads::fieldHeight };
}
} // namespace

/* ------------------------------------------------------------- arrival -- */

/* The number over a pad, as something to press: it draws nothing itself (the
 * Step does), takes the press so the pad does not, and says what it does. */
class Pads::Arrival final : public juce::Component
{
public:
    Arrival (Pads& p, int i) : pads (p), index (i)
    {
        ni::ui::setInfo (*this, info::arrival);
        setTitle ("Arrival of step " + juce::String (index + 1));
        setMouseCursor (juce::MouseCursor::IBeamCursor);
    }

    void mouseDown (const juce::MouseEvent&) override { pads.editArrival (index); }

private:
    Pads& pads;
    const int index;
};

/* ---------------------------------------------------------------- pads -- */

Pads::Pads (StepEdits& e) : edits (e)
{
    setTitle ("Pads");
    addAndMakeVisible (steps);
    for (int i = 0; i < maxSteps; ++i)
    {
        numbers[(std::size_t) i] = std::make_unique<Arrival> (*this, i);
        addChildComponent (*numbers[(std::size_t) i]);
    }
    addChildComponent (field);
    field.setTitle ("Arrival");
    ni::ui::setInfo (field, info::arrival);

    steps.describe = [this] (int i)
    {
        const auto mode = modes[(std::size_t) i];
        const char* state = mode == StepMode::tie ? "tie" : mode == StepMode::on ? "on" : "off";
        auto name = "Step " + juce::String (i + 1) + ", " + state;
        if (mode != StepMode::off)
            name << ", " << juce::roundToInt (depths[(std::size_t) i] * 100.0f) << " %";
        return name;
    };
    steps.setStepInfo (info::pads);

    steps.onPress = [this] (int i, bool shift) { return edits.press (i, shift); };
    steps.onDrag = [this] (int i, float amount) { edits.drag (i, amount); };
    steps.onToggle = [this] (int i, bool tie) { edits.toggle (i, tie); };
    steps.onNudge = [this] (int i, float delta) { edits.nudge (i, delta); };

    field.onCommit = [this] (const juce::String& typed)
    {
        if (editing >= 0)
            edits.typeRank (editing, typed);
    };
    field.onClose = [this] { closeField(); };
}

Pads::~Pads() = default;

juce::Component* Pads::arrival (int index)
{
    if (index < 0 || index >= maxSteps || ! numbers[(std::size_t) index]->isVisible())
        return nullptr;
    return numbers[(std::size_t) index].get();
}

void Pads::show (const Pattern& p, int playStep, bool fading)
{
    const int count = juce::jlimit (1, maxSteps, p.length);
    steps.setCount (count);

    const bool ordering = edits.ordering();
    const bool numbered = ordering || fading;
    bool renamed = false;
    for (int i = 0; i < count; ++i)
    {
        const auto at = (std::size_t) i;
        const bool arriving = edits.arriving (i);
        ni::ui::Step::State s;
        s.drawn = drawnOf (p.steps[at]);
        s.amount = p.depths[at];
        s.level = p.levels[at];
        s.play = i == playStep;
        s.beat = i % 4 == 0;
        s.cursor = i == p.cursor;
        s.waiting = ordering && arriving && ! edits.isNamed (i);
        s.number = numbered && arriving ? juce::jmax (0, p.orders[at]) : 0;
        steps.setState (i, s);

        auto& number = *numbers[at];
        number.setVisible (s.number > 0);
        if (s.number > 0)
            number.setBounds (numberBox (steps.stepBounds (i).translated (steps.getX(), steps.getY()), s.number));

        renamed = renamed || modes[at] != p.steps[at] || ! juce::exactlyEqual (depths[at], p.depths[at]);
        modes[at] = p.steps[at];
        depths[at] = p.depths[at];
    }
    for (int i = count; i < maxSteps; ++i)
        numbers[(std::size_t) i]->setVisible (false);
    if (renamed)
        steps.refreshNames();

    if (padsOrderLine != ordering)
    {
        padsOrderLine = ordering;
        steps.setStepInfo (ordering ? info::padsOrder : info::pads);
    }

    /* A field over a step the pattern no longer has is closed, unkept. */
    if (editing >= count)
        field.abandon();
}

void Pads::editArrival (int index, const juce::String& typed)
{
    if (index < 0 || index >= steps.getCount())
    {
        field.abandon();
        return;
    }
    if (field.isOpen())
        field.commit();
    editing = index;
    editingFromKeys = typed.isNotEmpty();
    const auto box = steps.stepBounds (index).translated (steps.getX(), steps.getY());
    field.setBounds (box.getX(), box.getY(), fieldWidth, fieldHeight);
    field.toFront (false);
    field.open (editingFromKeys ? typed : juce::String (juce::jmax (1, steps.getState (index).number)));
    if (editingFromKeys)
        field.setCaretPosition (typed.length());
    field.setTitle ("Arrival of step " + juce::String (index + 1));
}

bool Pads::keyPressed (const juce::KeyPress& k)
{
    /* The digit by its key, on the row or the number pad: the text a key
     * makes depends on the layout and on Shift. */
    const int code = k.getKeyCode();
    int digit = -1;
    if (code >= '0' && code <= '9')
        digit = code - '0';
    else if (code >= juce::KeyPress::numberPad0 && code <= juce::KeyPress::numberPad9)
        digit = code - juce::KeyPress::numberPad0;
    const auto mods = k.getModifiers();
    if (digit < 0 || mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;
    const int index = steps.focusedStep();
    if (arrival (index) == nullptr)
        return false;
    editArrival (index, juce::String (digit));
    return true;
}

void Pads::closeField()
{
    const int was = editing;
    editing = -1;
    field.setVisible (false);
    ni::ui::relight (*this);
    /* Opened from the keyboard, the keyboard goes back where it was. */
    if (editingFromKeys && was >= 0)
        steps.moveFocus (was);
    editingFromKeys = false;
}

void Pads::resized()
{
    steps.setBounds (getLocalBounds());
}

void Pads::paint (juce::Graphics& g)
{
    ni::ui::paintChildLights (g, *this);
}

void Pads::paintLight (juce::Graphics& g)
{
    ni::ui::forwardChildLights (g, *this);
}

} // namespace ni::tg
