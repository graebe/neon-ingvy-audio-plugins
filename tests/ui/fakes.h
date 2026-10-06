// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Stand-ins for the plugin, for the editors' tests: host parameters a test
 * owns and watches, and a model with nothing behind it.
 *
 * REAL PARAMETERS, FAKE OWNER. A FakeParameters holds genuine
 * juce::RangedAudioParameter objects -- AudioParameterFloat, Int, Choice, Bool
 * -- indexed as a processor would index them, with the plugin's text supplied
 * by the test the way the processor will supply the engine's. What it fakes is
 * the host: every gesture and every value write is recorded, in order, so a
 * test can say "one drag was one gesture" and mean exactly that.
 *
 * Use the index order of the product's real parameters (Model.h's enum, the
 * iPlug2 order the VST3 IDs are): a fake in another order tests another
 * plugin.
 */
#pragma once

#include "EditorModel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <deque>
#include <memory>
#include <vector>

namespace ni::ui::test
{

class FakeParameters final : private juce::AudioProcessorParameter::Listener
{
public:
    FakeParameters() = default;

    ~FakeParameters() override
    {
        for (auto& p : params)
            p->removeListener (this);
    }

    /* Adds a parameter at the next index, and watches it. */
    template <typename Param>
    Param& add (std::unique_ptr<Param> p)
    {
        auto& ref = *p;
        ref.setParameterIndex ((int) params.size());
        ref.addListener (this);
        params.push_back (std::move (p));
        return ref;
    }

    /* A continuous parameter with the plugin's text: `text` formats a plain
     * value, `parse` reads one back (and returns the current value for text
     * it cannot read, as the plugin's does). */
    juce::AudioParameterFloat& addFloat (const juce::String& id, const juce::String& name,
                                         juce::NormalisableRange<float> range, float def,
                                         std::function<juce::String (float)> text,
                                         std::function<float (const juce::String&)> parse)
    {
        auto attributes = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([text] (float v, int) { return text (v); })
            .withValueFromStringFunction ([parse] (const juce::String& s) { return parse (s); });
        return add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                                 range, def, attributes));
    }

    juce::AudioParameterChoice& addChoice (const juce::String& id, const juce::String& name,
                                           const juce::StringArray& options, int def)
    {
        return add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name,
                                                                  options, def));
    }

    juce::AudioParameterBool& addBool (const juce::String& id, const juce::String& name, bool def)
    {
        return add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    }

    juce::AudioParameterInt& addInt (const juce::String& id, const juce::String& name,
                                     int min, int max, int def)
    {
        return add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name,
                                                               min, max, def));
    }

    int size() const noexcept { return (int) params.size(); }
    juce::RangedAudioParameter& operator[] (int i) const { return *params[(size_t) i]; }

    /* What the host saw, in order. */
    struct Event
    {
        enum class Kind { begin, value, end };
        Kind kind;
        int index;
        float value;
    };
    std::vector<Event> events;

    /* The log as one line, for a CHECK that reads like the sequence:
     * "begin 0, value 0 0.5, end 0". Values to three places. */
    juce::String log() const
    {
        juce::StringArray out;
        for (const auto& e : events)
        {
            const auto i = juce::String (e.index);
            switch (e.kind)
            {
                case Event::Kind::begin: out.add ("begin " + i); break;
                case Event::Kind::end:   out.add ("end " + i); break;
                case Event::Kind::value: out.add ("value " + i + " " + juce::String (e.value, 3)); break;
            }
        }
        return out.joinIntoString (", ");
    }

    void clear() { events.clear(); }

private:
    void parameterValueChanged (int index, float value) override
    {
        events.push_back ({ Event::Kind::value, index, value });
    }

    void parameterGestureChanged (int index, bool starting) override
    {
        events.push_back ({ starting ? Event::Kind::begin : Event::Kind::end, index, 0.0f });
    }

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
};

/*
 * The common half of every editor's model: parameters, rings and the Motion
 * switch, with nothing behind them. A product's fake model derives from its
 * Model.h and can hold one of these for the shared part.
 */
class FakeEditorModel : public ni::ui::EditorModel
{
public:
    FakeParameters params;
    std::deque<float> rings;
    bool motionOn = true;

    int numParameters() const override { return params.size(); }
    juce::RangedAudioParameter& parameter (int index) override { return params[index]; }

    int takeRings (float* strengths, int capacity) override
    {
        int n = 0;
        while (n < capacity && ! rings.empty())
        {
            strengths[n++] = rings.front();
            rings.pop_front();
        }
        return n;
    }

    bool motion() const override { return motionOn; }
    void setMotion (bool on) override { motionOn = on; }
};

} // namespace ni::ui::test
