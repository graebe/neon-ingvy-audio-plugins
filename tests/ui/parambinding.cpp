// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A host parameter bound to a control: what the host sees for a drag, a
 * click, a key, a reset and a typed value -- and what the control hears when
 * the host moves the value instead.
 */
#include "ParamBinding.h"
#include "fakes.h"

#include <doctest.h>

using ni::ui::ParamBinding;
using ni::ui::test::FakeParameters;

namespace
{
/* An Attack-like time: 0..1000 ms, default 10, the plugin's own text. */
juce::AudioParameterFloat& addAttack (FakeParameters& params)
{
    return params.addFloat ("attack", "Attack", { 0.0f, 1000.0f }, 10.0f,
                            [] (float ms) { return juce::String (ms, 1) + " ms"; },
                            [] (const juce::String& s) { return s.upToFirstOccurrenceOf (" ", false, false).getFloatValue(); });
}
} // namespace

TEST_CASE ("parameter binding: a drag is one gesture, opened on the first move")
{
    FakeParameters params;
    auto& attack = addAttack (params);
    ParamBinding binding (attack);

    binding.begin();
    binding.input (0.25f);
    binding.input (0.5f);
    binding.end();

    CHECK (params.log() == "begin 0, value 0 0.250, value 0 0.500, end 0");
    CHECK (binding.value() == doctest::Approx (0.5f));
    CHECK_FALSE (binding.inGesture());
}

TEST_CASE ("parameter binding: a click is one complete gesture, and a click that changes nothing writes nothing")
{
    FakeParameters params;
    ParamBinding binding (addAttack (params));

    binding.commit (0.75f);
    CHECK (params.log() == "begin 0, value 0 0.750, end 0");

    params.clear();
    binding.commit (0.75f);
    CHECK (params.log() == "");
}

TEST_CASE ("parameter binding: a reset goes to the plugin's default, not the bottom of the range")
{
    FakeParameters params;
    auto& attack = addAttack (params);
    ParamBinding binding (attack);

    binding.commit (0.9f);
    binding.reset();
    CHECK (attack.get() == doctest::Approx (10.0f));
    CHECK (binding.value() == doctest::Approx (binding.defaultValue()));
}

TEST_CASE ("parameter binding: the text is the plugin's, both ways")
{
    FakeParameters params;
    auto& attack = addAttack (params);
    ParamBinding binding (attack);

    CHECK (binding.text() == "10.0 ms");
    binding.setText ("  250 ms ");
    CHECK (attack.get() == doctest::Approx (250.0f));
    CHECK (binding.text() == "250.0 ms");
    CHECK (binding.textFor (1.0f) == "1000.0 ms");
    CHECK (binding.steps() == 0);
}

TEST_CASE ("parameter binding: a choice has its options as steps")
{
    FakeParameters params;
    auto& curve = params.addChoice ("curve", "Env Curve", { "Linear", "Exponential", "S-Curve" }, 0);
    ParamBinding binding (curve);

    CHECK (binding.steps() == 3);
    CHECK (binding.choices() == juce::StringArray { "Linear", "Exponential", "S-Curve" });

    /* The last option is at 1.0: (n - 1) is the divisor. */
    binding.commit (1.0f);
    CHECK (curve.getIndex() == 2);
    CHECK (binding.text() == "S-Curve");
}

TEST_CASE ("parameter binding: a stepped parameter with no value-string list still has its steps as options")
{
    /* JUCE's own integer calls itself continuous (isDiscrete is false), so
     * its getAllValueStrings is empty -- and it still has steps, each with
     * its text. The options come from those, whatever the list says (UT3). */
    FakeParameters params;
    auto& bus = params.addInt ("bus", "Bus", 1, 4, 2);
    REQUIRE (bus.getAllValueStrings().isEmpty());
    ParamBinding binding (bus);

    CHECK (binding.steps() == 4);
    CHECK (binding.choices() == juce::StringArray { "1", "2", "3", "4" });

    binding.commit (1.0f);
    CHECK (bus.get() == 4);
    CHECK (binding.text() == "4");
}

TEST_CASE ("parameter binding: a change from the host reaches the control")
{
    FakeParameters params;
    auto& attack = addAttack (params);
    int heard = 0;
    ParamBinding binding (attack, [&] { ++heard; });

    attack.setValueNotifyingHost (0.3f);   // automation, on the message thread
    CHECK (heard == 1);
    CHECK (binding.value() == doctest::Approx (0.3f));
}

TEST_CASE ("parameter binding: a control destroyed mid-drag closes its gesture")
{
    FakeParameters params;
    auto& attack = addAttack (params);
    {
        ParamBinding binding (attack);
        binding.begin();
        binding.input (0.4f);
    }
    CHECK (params.log() == "begin 0, value 0 0.400, end 0");
}
