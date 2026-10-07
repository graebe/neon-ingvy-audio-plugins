// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Knob: a drag that opens its gesture on the first move, Shift finer,
 * detents that hold and that Page jumps between, a double-click that resets,
 * every key one edit, Enter and a click typing into the readout -- and the
 * same knob bound to a host parameter (ParamKnob), with the select and the
 * switch beside it, as the host sees each of them. Then every state, as a
 * picture.
 */
#include "Knob.h"
#include "ParamControls.h"

#include "Gallery.h"
#include "Pointer.h"
#include "checks.h"
#include "fakes.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::Knob;
using ni::ui::gallery::Pointer;
using ni::ui::test::FakeParameters;

namespace
{
void settle()
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}

/* A knob that writes down what it asked for, in order. */
struct Recorded
{
    Recorded()
    {
        knob.setBounds (0, 0, 96, Knob::cardHeight);
        knob.setValue (0.5f);
        knob.onBegin = [this] { log.add ("begin"); };
        knob.onInput = [this] (float v) { log.add ("input " + juce::String (v, 3)); last = v; };
        knob.onEnd = [this] { log.add ("end"); };
        knob.onCommit = [this] (float v) { log.add ("commit " + juce::String (v, 3)); };
        knob.onReset = [this] { log.add ("reset"); };
        knob.onText = [this] (const juce::String& t) { log.add ("text " + t); };
    }

    juce::String said() const { return log.joinIntoString (", "); }

    /* The dial's centre, in the dial's coordinates. */
    juce::Point<float> centre() const { return { 24.0f, 24.0f }; }

    Knob knob { "Attack" };
    juce::StringArray log;
    float last = -1.0f;
};
} // namespace

TEST_CASE ("knob: the card is label, dial and readout, space-2 apart, 106px tall")
{
    Knob k ("Rate");
    k.setBounds (0, 0, 96, Knob::cardHeight);
    CHECK (Knob::cardHeight == 106);
    CHECK (k.dialBounds() == juce::Rectangle<int> (24, 22, 48, 48));
    CHECK (k.readout().getBounds() == juce::Rectangle<int> (0, 78, 96, 28));
    CHECK (k.dial().getWantsKeyboardFocus());
    CHECK_FALSE (k.readout().getWantsKeyboardFocus());
}

TEST_CASE ("knob: a press is no edit; the gesture opens on the first move and closes on release")
{
    Recorded r;
    Pointer p;

    p.click (r.knob.dial(), r.centre());
    CHECK (r.said() == "");

    p.down (r.knob.dial(), r.centre());
    p.drag (r.centre().translated (0.0f, -20.0f));   // 20px of 200 up
    p.drag (r.centre().translated (0.0f, -40.0f));
    p.up();
    CHECK (r.said() == "begin, input 0.600, input 0.700, end");
}

TEST_CASE ("knob: Shift is five times finer, chosen at the press")
{
    Recorded r;
    Pointer p;
    p.down (r.knob.dial(), r.centre(), juce::ModifierKeys::shiftModifier);
    p.drag (r.centre().translated (0.0f, -100.0f));
    p.up();
    CHECK (r.last == doctest::Approx (0.6f));
}

TEST_CASE ("knob: a drag holds on a detent, and Shift ignores it")
{
    Recorded r;
    r.knob.setValue (0.4f);
    r.knob.setDetents ({ 0.5 });
    Pointer p;

    /* 20px up is the detent at 0.5; 10px more is still on it -- the hold is
     * 14px -- where without the detent it would be 0.55. */
    p.down (r.knob.dial(), r.centre());
    p.drag (r.centre().translated (0.0f, -30.0f));
    CHECK (r.last == doctest::Approx (0.5f));
    p.up();

    p.down (r.knob.dial(), r.centre(), juce::ModifierKeys::shiftModifier);
    p.drag (r.centre().translated (0.0f, -100.0f));
    CHECK (r.last == doctest::Approx (0.5f));   // 0.4 + 100/1000: through it, not held on it
    p.drag (r.centre().translated (0.0f, -110.0f));
    CHECK (r.last == doctest::Approx (0.51f));
    p.up();
}

TEST_CASE ("knob: a double-click resets, with no gesture around it")
{
    Recorded r;
    Pointer p;
    p.doubleClick (r.knob.dial(), r.centre());
    CHECK (r.said() == "reset");
}

TEST_CASE ("knob: every key is one edit -- arrows, Shift, Page to the detents, Home and End")
{
    Recorded r;
    r.knob.setDetents ({ 0.25, 0.75 });
    auto& dial = r.knob.dial();
    using ni::ui::gallery::key;

    CHECK (key (dial, juce::KeyPress::upKey));
    CHECK (key (dial, juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier));
    CHECK (key (dial, juce::KeyPress::downKey));
    CHECK (key (dial, juce::KeyPress::pageUpKey));
    CHECK (key (dial, juce::KeyPress::pageDownKey));
    CHECK (key (dial, juce::KeyPress::homeKey));
    CHECK (key (dial, juce::KeyPress::endKey));
    CHECK_FALSE (key (dial, juce::KeyPress::tabKey));
    CHECK (r.said() == "commit 0.510, commit 0.502, commit 0.490, commit 0.750, commit 0.250, "
                       "commit 0.000, commit 1.000");

    /* Past the last detent, a page is a tenth of the range. */
    r.log.clear();
    r.knob.setValue (0.8f);
    key (dial, juce::KeyPress::pageUpKey);
    CHECK (r.said() == "commit 0.900");
}

TEST_CASE ("knob: Enter types into the readout, and what is typed goes to the owner once")
{
    Recorded r;
    r.knob.setValueText ("12.5 ms");
    ni::ui::gallery::key (r.knob.dial(), juce::KeyPress::returnKey);
    REQUIRE (r.knob.readout().isBeingEdited());

    auto* field = r.knob.readout().getCurrentTextEditor();
    REQUIRE (field != nullptr);
    field->setText ("40 ms", false);
    field->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    settle();
    CHECK (r.said() == "text 40 ms");
    CHECK_FALSE (r.knob.readout().isBeingEdited());
}

TEST_CASE ("knob: disabled, it neither drags nor takes keys")
{
    Recorded r;
    r.knob.setEnabled (false);
    Pointer p;
    p.down (r.knob.dial(), r.centre());
    p.drag (r.centre().translated (0.0f, -20.0f));
    p.up();
    CHECK_FALSE (ni::ui::gallery::key (r.knob.dial(), juce::KeyPress::upKey));
    CHECK (r.said() == "");
}

TEST_CASE ("knob: a screen reader hears a slider with the plugin's text")
{
    Recorded r;
    r.knob.setValueText ("1/16");
    /* getAccessibilityHandler() answers only on screen; this is what it makes. */
    const auto handler = r.knob.dial().createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::slider);
    REQUIRE (handler->getValueInterface() != nullptr);
    CHECK (handler->getValueInterface()->getCurrentValueAsString() == "1/16");
    handler->getValueInterface()->setValue (0.25);
    CHECK (r.said() == "commit 0.250");
}

/* -------------------------------------------------- bound to parameters -- */

namespace
{
juce::AudioParameterFloat& addPercent (FakeParameters& params, const juce::String& id, float def)
{
    return params.addFloat (id, id, { 0.0f, 100.0f }, def,
                            [] (float v) { return juce::String (v, 2) + " %"; },
                            [] (const juce::String& s) { return s.getFloatValue(); });
}
} // namespace

TEST_CASE ("param knob: a drag is one gesture in the host, a click none, a double-click the default")
{
    FakeParameters params;
    auto& amount = addPercent (params, "amount", 100.0f);
    ni::ui::ParamKnob knob (amount, "Amount");
    knob.setBounds (0, 0, 96, Knob::cardHeight);
    CHECK (knob.getValueText() == "100.00 %");
    CHECK (knob.getValue() == doctest::Approx (1.0f));

    Pointer p;
    p.click (knob.dial(), { 24.0f, 24.0f });
    CHECK (params.log() == "");

    p.down (knob.dial(), { 24.0f, 24.0f });
    p.drag ({ 24.0f, 44.0f });   // 20px down
    p.up();
    CHECK (params.log() == "begin 0, value 0 0.900, end 0");
    CHECK (knob.getValueText() == "90.00 %");

    params.clear();
    p.doubleClick (knob.dial(), { 24.0f, 24.0f });
    CHECK (params.log() == "begin 0, value 0 1.000, end 0");
}

TEST_CASE ("param knob: typed text is the plugin's to read; a text source overrides both ways")
{
    FakeParameters params;
    auto& attack = addPercent (params, "attack", 10.0f);
    ni::ui::ParamKnob knob (attack, "Attack");

    knob.onText ("25");
    CHECK (params.log() == "begin 0, value 0 0.250, end 0");

    /* The readout in milliseconds of a 200 ms gate, as the Trance Gate's
     * stages read when Time is ms. */
    params.clear();
    knob.setText ([&] { return juce::String (attack.get() * 2.0f, 1) + " ms"; },
                  [] (const juce::String& s) -> std::optional<float>
                  {
                      if (! s.containsAnyOf ("0123456789"))
                          return std::nullopt;
                      return s.getFloatValue() / 200.0f;
                  });
    CHECK (knob.getValueText() == "50.0 ms");
    knob.onText ("40 ms");
    CHECK (params.log() == "begin 0, value 0 0.200, end 0");
    CHECK (knob.getValueText() == "40.0 ms");

    params.clear();
    knob.onText ("nonsense");
    CHECK (params.log() == "");
    CHECK (knob.getValueText() == "40.0 ms");
}

TEST_CASE ("param knob: a change from the host moves it")
{
    FakeParameters params;
    auto& amount = addPercent (params, "amount", 100.0f);
    ni::ui::ParamKnob knob (amount, "Amount");
    amount.setValueNotifyingHost (0.25f);
    CHECK (knob.getValue() == doctest::Approx (0.25f));
    CHECK (knob.getValueText() == "25.00 %");
}

TEST_CASE ("param select: the parameter's choices, a choice committed, the host's shown")
{
    FakeParameters params;
    auto& curve = params.addChoice ("curve", "Env Curve", { "Linear", "Exponential", "S-Curve" }, 0);
    ni::ui::ParamSelect select (curve);
    CHECK (select.getOptions() == juce::StringArray { "Linear", "Exponential", "S-Curve" });
    CHECK (select.getIndex() == 0);

    select.onChange (2);
    CHECK (params.log() == "begin 0, value 0 1.000, end 0");
    CHECK (select.getIndex() == 2);

    curve.setValueNotifyingHost (0.5f);
    CHECK (select.getIndex() == 1);
}

TEST_CASE ("param toggle: a parameter of two is a switch, on at the second")
{
    FakeParameters params;
    auto& dir = params.addChoice ("fadeDir", "Fade Dir", { "In", "Out" }, 0);
    ni::ui::ParamToggle out (dir, "Out");
    out.setBounds (0, 0, out.idealWidth(), 28);
    CHECK_FALSE (out.isOn());

    Pointer p;
    p.click (out, { 10.0f, 14.0f });
    CHECK (params.log() == "begin 0, value 0 1.000, end 0");
    CHECK (out.isOn());

    dir.setValueNotifyingHost (0.0f);
    CHECK_FALSE (out.isOn());
}

NI_SNAPSHOT_TEST ("knob: every state of the card")
{
    ni::ui::gallery::Frame frame (ni::ui::gallery::pages());
    REQUIRE (frame.show (juce::String ("controls-knob")));
    REQUIRE (frame.page() != nullptr);
    NI_CHECK_INFO_LIMIT (*frame.page());
    NI_CHECK_SNAPSHOT (*frame.page(), "controls-knob");
}
