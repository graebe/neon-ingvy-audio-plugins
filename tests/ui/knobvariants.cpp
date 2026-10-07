// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Knob's two variants that Ultraviolet 1.1.0 names and the Side-Chain
 * needs first: the bipolar arc (Knob card), drawn from 12 o'clock, and the
 * stepped knob for a choice too long for a Select (Select card), whose keys
 * move whole options.
 */
#include "Knob.h"
#include "ParamChoiceKnob.h"

#include "Pointer.h"
#include "UvTokens.h"
#include "fakes.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::Knob;
using ni::ui::ParamChoiceKnob;
using ni::ui::gallery::key;
using ni::ui::test::FakeParameters;

namespace
{
/* Whether the rail at `degrees` clockwise from 12 o'clock is lit: the pixel on
 * the arc's radius, in the dial's 48px box, is the value colour rather than
 * the rail's. */
bool litAt (Knob& knob, float degrees)
{
    const auto image = ni::ui::test::render (knob.dial());
    const auto at = juce::Point<float> (24.0f, 24.0f)
                        .getPointOnCircumference (20.0f, juce::degreesToRadians (degrees));
    const auto pixel = image.getPixelAt (juce::roundToInt (at.x), juce::roundToInt (at.y));
    /* uv is near-white, line-200 a dark violet: brightness tells them apart. */
    return pixel.getBrightness() > uv::tok::colour::line200.getBrightness() + 0.3f;
}
} // namespace

TEST_CASE ("knob: unipolar, the arc runs from the minimum")
{
    Knob knob ("Depth");
    knob.setBounds (0, 0, 64, Knob::cardHeight);
    knob.setValue (0.5f);
    CHECK_FALSE (knob.isBipolar());
    CHECK (litAt (knob, -100.0f));   // between the minimum and 12 o'clock
    CHECK_FALSE (litAt (knob, 60.0f));
}

TEST_CASE ("knob: bipolar, the arc runs from 12 o'clock -- left below the centre, right above, none at it")
{
    Knob knob ("Delay");
    knob.setBounds (0, 0, 64, Knob::cardHeight);
    knob.setBipolar (true);
    CHECK (knob.isBipolar());

    knob.setValue (0.5f);
    CHECK_FALSE (litAt (knob, -60.0f));
    CHECK_FALSE (litAt (knob, 60.0f));
    CHECK_FALSE (litAt (knob, -100.0f));

    knob.setValue (0.25f);           // -50 %: 67.5 degrees left of 12
    CHECK (litAt (knob, -40.0f));
    CHECK_FALSE (litAt (knob, -100.0f));
    CHECK_FALSE (litAt (knob, 40.0f));

    knob.setValue (1.0f);            // +100 %: to the end of the rail
    CHECK (litAt (knob, 40.0f));
    CHECK (litAt (knob, 120.0f));
    CHECK_FALSE (litAt (knob, -40.0f));
}

TEST_CASE ("choice knob: a key moves at least one option, Page a tenth of the list, Home and End the ends")
{
    CHECK (ParamChoiceKnob::stepFrom (1.0f / 16.0f, 1.0f / 16.0f + 0.01f, 17) == doctest::Approx (2.0f / 16.0f));
    CHECK (ParamChoiceKnob::stepFrom (2.0f / 16.0f, 2.0f / 16.0f - 0.002f, 17) == doctest::Approx (1.0f / 16.0f));
    CHECK (ParamChoiceKnob::stepFrom (36.0f / 127.0f, 36.0f / 127.0f + 0.1f, 128) == doctest::Approx (49.0f / 127.0f));
    CHECK (ParamChoiceKnob::stepFrom (0.5f, 0.0f, 17) == 0.0f);
    CHECK (ParamChoiceKnob::stepFrom (0.5f, 1.0f, 17) == 1.0f);
    /* At the bottom, a step down stays there. */
    CHECK (ParamChoiceKnob::stepFrom (0.0f, 0.0f, 17) == 0.0f);
    /* No choice, no steps: the value as asked. */
    CHECK (ParamChoiceKnob::stepFrom (0.2f, 0.3f, 0) == doctest::Approx (0.3f));
}

TEST_CASE ("choice knob: bound to a choice, each arrow is one option and one edit in the host")
{
    FakeParameters params;
    juce::StringArray channels { "Omni" };
    for (int i = 1; i <= 16; ++i)
        channels.add (juce::String (i));
    auto& channel = params.addChoice ("channel", "Channel", channels, 1);

    ParamChoiceKnob knob (channel, "Ch");
    knob.setBounds (0, 0, 64, Knob::cardHeight);
    CHECK (knob.getValueText() == "1");

    CHECK (key (knob.dial(), juce::KeyPress::upKey));
    CHECK (knob.getValueText() == "2");
    CHECK (params.log() == "begin 0, value 0 0.125, end 0");

    params.clear();
    CHECK (key (knob.dial(), juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier));
    CHECK (key (knob.dial(), juce::KeyPress::downKey));
    CHECK (knob.getValueText() == "Omni");
    CHECK (params.log() == "begin 0, value 0 0.062, end 0, begin 0, value 0 0.000, end 0");

    params.clear();
    CHECK (key (knob.dial(), juce::KeyPress::endKey));
    CHECK (knob.getValueText() == "16");

    /* A drag lands on an option: the parameter holds the nearest. */
    params.clear();
    ni::ui::gallery::Pointer p;
    p.down (knob.dial(), { 24.0f, 24.0f });
    p.drag ({ 24.0f, 44.0f });           // 20px down, a tenth of the range
    p.up();
    CHECK (params.events.front().kind == FakeParameters::Event::Kind::begin);
    CHECK (knob.getValueText() == "14");
}

NI_SNAPSHOT_TEST ("knob: bipolar at -100, -50, 0, +50 and +100 %")
{
    juce::Component row;
    row.setSize (5 * 72, Knob::cardHeight);
    std::vector<std::unique_ptr<Knob>> knobs;
    const float values[] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    const char* texts[] { "-100.0 %", "-50.0 %", "0.0 %", "50.0 %", "100.0 %" };
    for (int i = 0; i < 5; ++i)
    {
        auto& k = *knobs.emplace_back (std::make_unique<Knob> ("Delay"));
        k.setBipolar (true);
        k.setValue (values[i]);
        k.setValueText (texts[i]);
        row.addAndMakeVisible (k);
        k.setBounds (i * 72 + 4, 0, 64, Knob::cardHeight);
    }
    NI_CHECK_SNAPSHOT (row, "knob-bipolar");
}
