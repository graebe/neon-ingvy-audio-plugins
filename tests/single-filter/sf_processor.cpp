// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The single filters' processor on the real engine, for all nine products:
 * what only the shell around it can get wrong.
 *
 *   the parameters  declared from the product's table: its keys as IDs, its
 *                   names, ranges, defaults and words; typing read by the
 *                   engine; the analyzer's mode a choice no host automates;
 *                   every field the editor asks for where the engine says
 *   the buses       mono or stereo, the output as the input
 *   the sound       each product does what it is named for, at its
 *                   parameters; the host's bypass passes the audio; bypassed
 *                   past two seconds the engine idles, and comes back heard
 *   the state       saved and reopened, every value exactly, and each
 *                   product's v1 fixture
 *   the model       the picture designed from the host's values
 *   the window      opens at the design's size with only the product's
 *                   controls, Output in the last column, and lights what the
 *                   product uses
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "Editor.h"
#include "SingleFilter.h"

#include "state_fixture.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <set>

using ni::sf::Processor;
namespace field = ni::sf::field;
namespace shape = ni::sf::shape;

namespace
{
constexpr int block = 512;
constexpr double rate = 48000.0;

struct Product
{
    int shape;
    const char* key;   // versions.json, plugins/<key>
    const char* name;  // after "NI "
    int count;
};

constexpr Product products[] = {
    { shape::highPass, "high-pass", "High Pass", 10 }, { shape::lowPass, "low-pass", "Low Pass", 10 },
    { shape::bandPass, "band-pass", "Band Pass", 9 },  { shape::notch, "notch", "Notch", 9 },
    { shape::bell, "bell", "Bell", 7 },                { shape::lowShelf, "low-shelf", "Low Shelf", 7 },
    { shape::highShelf, "high-shelf", "High Shelf", 7 }, { shape::tilt, "tilt", "Tilt", 5 },
    { shape::allPass, "all-pass", "All Pass", 6 },
};

struct Instance
{
    Processor p;
    juce::AudioBuffer<float> buffer { 2, block };
    juce::int64 n = 0;

    explicit Instance (int s, int channels = 2) : p (s)
    {
        if (channels == 1)
        {
            auto layout = p.getBusesLayout();
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
            layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
            REQUIRE (p.setBusesLayout (layout));
            buffer.setSize (1, block);
        }
        p.prepareToPlay (rate, block);
    }

    /* Field `f` to `value`; a field the product lacks is skipped. */
    void set (int f, double value)
    {
        const int index = p.indexOf (f);
        if (index < 0)
            return;
        auto& prm = p.parameter (index);
        prm.setValueNotifyingHost (prm.convertTo0to1 ((float) value));
    }

    /* `blocks` blocks of a sine at `hz`, the last block's level in dB
     * against the input's. */
    double run (double hz, int blocks = 1, bool bypassed = false)
    {
        double in = 0.0, out = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> dry (buffer.getNumChannels(), block);
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                for (int i = 0; i < block; ++i)
                {
                    const float x = (float) (0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) (n + i) / rate));
                    buffer.setSample (c, i, x);
                    dry.setSample (c, i, x);
                }
            juce::MidiBuffer midi;
            auto& host = static_cast<juce::AudioProcessor&> (p);
            if (bypassed)
                host.processBlockBypassed (buffer, midi);
            else
                host.processBlock (buffer, midi);
            n += block;
            if (b == blocks - 1)
            {
                in = dry.getRMSLevel (0, 0, block);
                out = buffer.getRMSLevel (0, 0, block);
            }
        }
        return 20.0 * std::log10 (out / in);
    }
};
} // namespace

TEST_CASE ("each product's parameters are its table, and the Bypass last")
{
    for (const auto& prod : products)
    {
        CAPTURE (prod.name);
        Processor p (prod.shape);
        CHECK (p.productName() == prod.name);
        REQUIRE (p.numParameters() == prod.count);
        REQUIRE (p.getParameters().size() == prod.count + 1);
        std::set<juce::String> ids;
        for (auto* each : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (each))
                ids.insert (withId->getParameterID());
        CHECK (ids.size() == (size_t) prod.count + 1);
        CHECK (p.getParameters().getLast() == p.getBypassParameter());
        CHECK (p.parameter (0).getParameterID() == "freq");
        CHECK (p.parameter (p.indexOf (field::output)).getName (64) == "Output");
        CHECK (! p.parameter (p.indexOf (field::analyzer)).isAutomatable());
        CHECK (p.indexOf (field::analyzer) == prod.count - 1);
        auto& f = p.parameter (0);
        f.setValueNotifyingHost (f.getValueForText ("2.5k"));
        CHECK (p.plain (0) == doctest::Approx (2500.0));
        CHECK (f.getCurrentValueAsText() == "2.50 kHz");
        const float before = f.getValue();
        f.setValueNotifyingHost (f.getValueForText ("loud"));
        CHECK (f.getValue() == before);
        CHECK (p.bipolar (p.indexOf (field::output)));
        CHECK (! p.bipolar (0));
    }
    /* What each has, and what it has not. */
    CHECK (Processor (shape::highPass).indexOf (field::gain) < 0);
    CHECK (Processor (shape::highPass).indexOf (field::matched) == 6);
    CHECK (Processor (shape::bell).indexOf (field::adaptiveQ) == 3);
    CHECK (Processor (shape::tilt).indexOf (field::q) < 0);
    Processor notch (shape::notch), shelf (shape::lowShelf), allPass (shape::allPass);
    auto* family = dynamic_cast<juce::AudioParameterChoice*> (&notch.parameter (3));
    REQUIRE (family != nullptr);
    CHECK (family->choices.size() == 5);
    auto* order = dynamic_cast<juce::AudioParameterChoice*> (&shelf.parameter (3));
    REQUIRE (order != nullptr);
    CHECK (order->choices.size() == 4);
    CHECK (order->name == "Slope");
    CHECK (allPass.parameter (2).getName (64) == "Order");
}

TEST_CASE ("mono or stereo, the output as the input")
{
    Processor p (shape::bell);
    auto layout = p.getBusesLayout();
    for (const auto& set : { juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo() })
    {
        layout.inputBuses.getReference (0) = set;
        layout.outputBuses.getReference (0) = set;
        CHECK (p.checkBusesLayoutSupported (layout));
    }
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    CHECK_FALSE (p.checkBusesLayoutSupported (layout));
    CHECK (p.getLatencySamples() == 0);
    CHECK_FALSE (p.acceptsMidi());
}

TEST_CASE ("each product does what it is named for")
{
    const auto at = [] (int s, std::initializer_list<std::pair<int, double>> settings, double hz, int channels = 2)
    {
        Instance in (s, channels);
        for (const auto& [f, v] : settings)
            in.set (f, v);
        return in.run (hz, 40);
    };
    CHECK (at (shape::highPass, { { field::freq, 1000.0 }, { field::order, 3.0 } }, 100.0) < -70.0);
    CHECK (at (shape::highPass, { { field::freq, 100.0 } }, 5000.0) == doctest::Approx (0.0).epsilon (0.01));
    CHECK (at (shape::lowPass, { { field::freq, 1000.0 }, { field::order, 3.0 } }, 10000.0) < -70.0);
    CHECK (at (shape::bandPass, { { field::freq, 1000.0 }, { field::q, 4.0 } }, 1000.0) == doctest::Approx (0.0).epsilon (0.05));
    CHECK (at (shape::bandPass, { { field::freq, 1000.0 }, { field::q, 4.0 } }, 100.0) < -20.0);
    CHECK (at (shape::notch, { { field::freq, 1000.0 }, { field::q, 2.0 } }, 1000.0) < -40.0);
    CHECK (at (shape::bell, { { field::gain, 6.0 } }, 1000.0) == doctest::Approx (6.0).epsilon (0.01));
    CHECK (at (shape::lowShelf, { { field::freq, 1000.0 }, { field::gain, -9.0 } }, 30.0) == doctest::Approx (-9.0).epsilon (0.01));
    CHECK (at (shape::highShelf, { { field::gain, 5.0 } }, 18000.0) == doctest::Approx (5.0).epsilon (0.03));
    /* A tilt is its two halves' mean at Freq: within a tenth of a dB. */
    CHECK (std::abs (at (shape::tilt, { { field::gain, 8.0 } }, 1000.0)) < 0.1);
    CHECK (at (shape::tilt, { { field::gain, 8.0 } }, 18000.0) > 3.0);
    CHECK (at (shape::allPass, { { field::order, 7.0 } }, 1000.0) == doctest::Approx (0.0).epsilon (0.01));
    CHECK (at (shape::bell, { { field::gain, 6.0 }, { field::output, -6.0 } }, 1000.0) == doctest::Approx (0.0).epsilon (0.01));
    /* On a mono track, a Side filter does nothing. */
    CHECK (std::abs (at (shape::bell, { { field::gain, 12.0 }, { field::channel, 4.0 } }, 1000.0, 1)) < 1e-4);
}

TEST_CASE ("the host's bypass passes; past two seconds the engine idles, and comes back heard")
{
    Instance in (shape::bell);
    in.set (field::gain, 12.0);
    CHECK (in.run (1000.0, 20) == doctest::Approx (12.0).epsilon (0.01));
    CHECK (std::abs (in.run (1000.0, 20, true)) < 0.01);
    /* Three seconds bypassed: idle; then heard again, from the music. */
    CHECK (std::abs (in.run (1000.0, (int) (3.0 * rate / block), true)) < 0.01);
    in.run (1000.0, 20);
    CHECK (in.run (1000.0, 1) == doctest::Approx (12.0).epsilon (0.01));
}

TEST_CASE ("the tail is the engine's: how long the filter rings on")
{
    Instance in (shape::notch);
    in.set (field::freq, 20.0);
    in.set (field::q, 40.0);
    in.set (field::order, 0.0);
    CHECK (in.p.getTailLengthSeconds() == doctest::Approx (4.4).epsilon (0.06));
    in.set (field::freq, 5000.0);
    CHECK (in.p.getTailLengthSeconds() < 0.05);
}

TEST_CASE ("the state comes back exactly, and is heard")
{
    for (const auto& prod : products)
    {
        CAPTURE (prod.name);
        Instance a (prod.shape);
        a.set (field::freq, 2345.6);
        a.set (field::gain, -4.2);
        a.set (field::q, 3.3);
        a.set (field::channel, 3.0);
        a.set (field::output, 1.5);
        const auto chunk = ni::test::saved (a.p);
        Instance b (prod.shape);
        ni::test::load (b.p, chunk);
        for (int i = 0; i < prod.count; ++i)
            CHECK (b.p.plain (i) == a.p.plain (i));
        CHECK (b.run (1000.0, 10) == doctest::Approx (a.run (1000.0, 10)).epsilon (1e-6));
    }
}

TEST_CASE ("the model's picture is the engine's design of the host's values")
{
    Instance in (shape::bell);
    in.set (field::freq, 2000.0);
    in.set (field::gain, 9.0);
    in.set (field::output, -3.0);
    const auto& pic = in.p.picture();
    CHECK (pic.handleHz == doctest::Approx (2000.0));
    CHECK (pic.handleDb == doctest::Approx (9.0));
    CHECK (pic.edgeHz[0] < 2000.0);
    CHECK (pic.edgeHz[1] > 2000.0);
    const auto k = (size_t) (std::lower_bound (pic.hz.begin(), pic.hz.end(), 2000.0f) - pic.hz.begin());
    CHECK (pic.curve[k] == doctest::Approx (9.0).epsilon (0.02));
    CHECK (pic.total[k] == doctest::Approx (pic.curve[k] - 3.0f));
    CHECK (in.p.uses (0, 0) == ni::sf::uses::q);
    CHECK (Processor (shape::highPass).uses (0, 0) == 0u);
    CHECK (Processor (shape::highPass).orderPlayed (5, 2) == 3);
    CHECK (in.p.qForEdge (1000.0, 1414.2) == doctest::Approx (1.414).epsilon (0.01));
}

TEST_CASE ("the analyzer's rings are kept only while a window shows the analyzer")
{
    Instance in (shape::lowPass);
    in.p.editorOpened();
    in.run (1000.0, 60);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    in.run (1000.0, 4);
    CHECK (in.p.picture().analyzer);
    in.set (field::analyzer, 0.0);
    in.p.picture();
    in.run (1000.0, 8);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    in.run (1000.0, 4);
    CHECK_FALSE (in.p.picture().analyzer);
    in.p.editorClosed();
}

TEST_CASE ("the window: its size, only the product's controls, Output in the last column")
{
    for (const auto& prod : products)
    {
        CAPTURE (prod.name);
        Processor p (prod.shape);
        ni::sf::Editor editor (p);
        CHECK (editor.getWidth() == ni::sf::Editor::designWidth);
        CHECK (editor.getHeight() == ni::sf::Editor::designHeight);
        CHECK (editor.panel().getTitleText() == prod.name);
        for (int f : { field::freq, field::gain, field::q, field::ripple, field::stopband, field::output })
            CHECK ((editor.knobFor (f) != nullptr) == (p.indexOf (f) >= 0));
        for (int f : { field::family, field::order, field::channel })
            CHECK ((editor.selectFor (f) != nullptr) == (p.indexOf (f) >= 0));
        CHECK ((editor.toggleFor (field::matched) != nullptr) == (p.indexOf (field::matched) >= 0));
        CHECK ((editor.toggleFor (field::adaptiveQ) != nullptr) == (p.indexOf (field::adaptiveQ) >= 0));
        /* Output sits in the same place in all nine. */
        auto* output = editor.knobFor (field::output);
        REQUIRE (output != nullptr);
        CHECK (output->getRight() == editor.panel().contentBounds().getRight());
        CHECK (editor.well().drawsPhase() == (prod.shape == shape::allPass));
    }
    /* What the product has but does not use now is disabled. */
    Processor cut (shape::lowPass);
    ni::sf::Editor e (cut);
    auto& order = cut.parameter (cut.indexOf (field::order));
    order.setValueNotifyingHost (order.convertTo0to1 (0.0f));
    e.tick (0.0);
    CHECK_FALSE (e.knobFor (field::q)->isEnabled());
    CHECK_FALSE (e.knobFor (field::ripple)->isEnabled());
    auto& family = cut.parameter (cut.indexOf (field::family));
    family.setValueNotifyingHost (family.convertTo0to1 (4.0f));
    order.setValueNotifyingHost (order.convertTo0to1 (3.0f));
    e.tick (0.0);
    CHECK (e.knobFor (field::q)->isEnabled());
    CHECK (e.knobFor (field::ripple)->isEnabled());
    CHECK (e.knobFor (field::stopband)->isEnabled());
    /* A Linkwitz-Riley cut is even: the slope's select snaps up. */
    e.selectFor (field::family)->onChange (5);
    e.selectFor (field::order)->onChange (2);
    CHECK (cut.plain (cut.indexOf (field::order)) == 3.0);
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    doctest::Context context;
    context.applyCommandLine (argc, argv);
    return context.run();
}

TEST_CASE ("each product's state saved at version 1 loads; a newer one is refused; a missing value is its default")
{
    const struct
    {
        const Product& product;
        std::initializer_list<ni::test::Setting> settings;
    } fixtures[] = {
        { products[0], { { "freq", 80.0 }, { "q", 1.2 }, { "order", 3.0 }, { "family", 4.0 }, { "matched", 1.0 }, { "output", -1.5 } } },
        { products[1], { { "freq", 6000.0 }, { "order", 5.0 }, { "family", 1.0 }, { "channel", 3.0 } } },
        { products[2], { { "freq", 700.0 }, { "q", 3.0 }, { "family", 2.0 }, { "ripple", 0.4 } } },
        { products[3], { { "freq", 3200.0 }, { "q", 6.0 }, { "family", 3.0 }, { "stopband", 72.0 } } },
        { products[4], { { "freq", 2500.0 }, { "gain", -5.5 }, { "q", 2.4 }, { "adaptive_q", 1.0 } } },
        { products[5], { { "freq", 150.0 }, { "gain", 4.0 }, { "order", 2.0 } } },
        { products[6], { { "freq", 9000.0 }, { "gain", -3.0 }, { "channel", 4.0 } } },
        { products[7], { { "freq", 600.0 }, { "gain", 3.5 }, { "output", -2.0 } } },
        { products[8], { { "freq", 450.0 }, { "q", 1.5 }, { "order", 6.0 } } },
    };
    for (const auto& fx : fixtures)
    {
        CAPTURE (fx.product.name);
        const int s = fx.product.shape;
        ni::test::checkStateFixture (
            [s] {
                auto p = std::make_unique<Processor> (s);
                p->prepareToPlay (48000.0, 512);
                return std::unique_ptr<juce::AudioProcessor> (std::move (p));
            },
            juce::File (NI_FIXTURES).getChildFile (juce::String (fx.product.key) + "/v1.state"), fx.settings);
    }
}
