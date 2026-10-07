// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain on the JUCE shell, its processor in the program: the
 * parameters a host sees, the state a set keeps, the buses, the MIDI and its
 * panic, the editor's model on the real engine, and the native editor driven
 * by that model.
 *
 *   the parameters  as the iPlug2 build reported them (parameters.json):
 *                   names, units, steps, defaults and default texts; the
 *                   rate table the engine's; every text read back
 *   the fixtures    every tests/fixtures/iplug2/NISideChain state loads to
 *                   its exact values, displays and bypass, and saves back
 *                   byte for byte
 *   the state       save, a fresh instance, load, save again: the same bytes;
 *                   what no build wrote changes nothing; a headerless chunk
 *                   opens; the bypass a chunk carries is restored
 *   the audio       the host's values reach the engine; Cycle ducks under a
 *                   running transport; a mono track; a key ducks on
 *                   Sidechain, and the key bus is reported as the host left it
 *   the MIDI        a note ducks from its own sample; CC 120 and CC 123 open
 *                   a held gate on any channel -- the panic -- and a note
 *                   released while bypassed does not hold the duck down
 *   the model       the `ui` and `stage_ms` readouts, the key bus, the
 *                   capture while a window is open, the Ground's rings, and
 *                   the shape and its marks -- the shape drawn is the duck the
 *                   engine plays
 *   the editor      on the real model: the header's state and warning, the
 *                   plugin window at the design's size, and a set loaded on
 *                   another thread while it is open
 *
 * What is the editor's own behaviour against a model that does as told is
 * tests/ui's (side-chain_editor.cpp, on fakes); this is what only the real
 * model can show. sc_host.cpp holds the built VST3, as a DAW hosts it.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "EngineModel.h"
#include "PluginEditor.h"
#include "SideChain.h"
#include "SideChainEditor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace ni::sc;
using Bytes = std::vector<std::uint8_t>;

namespace
{
const juce::File fixtures = juce::File (NI_FIXTURES).getChildFile ("NISideChain");
constexpr double rate = 48000.0;
constexpr int blockSize = 512;

Bytes fixture (const juce::String& scenario)
{
    juce::MemoryBlock b;
    REQUIRE (fixtures.getChildFile (scenario + ".component.bin").loadFileAsData (b));
    const auto* p = static_cast<const std::uint8_t*> (b.getData());
    return Bytes (p, p + b.getSize());
}

/* A transport playing at 120 BPM in 4/4 from bar 1, moved on by the caller:
 * one cycle at 1/4 is 500 ms, 24000 samples. */
struct Playhead final : juce::AudioPlayHead
{
    juce::int64 sample = 0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setBpm (120.0);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setTimeInSamples (sample);
        p.setPpqPosition ((double) sample / rate * 2.0);
        return p;
    }
};

/* One instance, as a host runs it: stereo in and out, the key off unless a
 * test connects it. */
struct Instance
{
    Processor p;
    Playhead head;
    bool keyed = false;

    explicit Instance (juce::AudioChannelSet key = juce::AudioChannelSet::disabled(),
                       juce::AudioChannelSet main = juce::AudioChannelSet::stereo())
    {
        auto layout = p.getBusesLayout();
        layout.inputBuses.getReference (0) = main;
        layout.outputBuses.getReference (0) = main;
        layout.inputBuses.getReference (1) = key;
        REQUIRE (p.setBusesLayout (layout));
        keyed = ! key.isDisabled();
        p.prepareToPlay (rate, blockSize);
    }

    EngineModel& model() { return p.model(); }
    double value (int i) { return p.parameter (i).plain(); }
    void set (int i, double v) { p.parameter (i).setPlainNotifyingHost (v); }

    /* A buffer as the wrapper hands one over: the main channels, then the
     * key's, the main filled with `level` and the key with silence. */
    juce::AudioBuffer<float> buffer (float level = 0.5f, int frames = blockSize)
    {
        const int channels = juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
        juce::AudioBuffer<float> b (channels, frames);
        b.clear();
        for (int ch = 0; ch < p.getMainBusNumInputChannels(); ++ch)
            juce::FloatVectorOperations::fill (b.getWritePointer (ch), level, frames);
        return b;
    }

    /* One block, under the transport when `playing`, with `midi`. */
    juce::AudioBuffer<float> block (juce::MidiBuffer midi = {}, bool playing = true, bool bypassed = false)
    {
        auto b = buffer();
        p.setPlayHead (playing ? &head : nullptr);
        if (bypassed)
            p.processBlockBypassed (b, midi);
        else
            p.processBlock (b, midi);
        head.sample += b.getNumSamples();
        p.setPlayHead (nullptr);
        return b;
    }

    void play (int blocks, bool playing = true)
    {
        for (int i = 0; i < blocks; ++i)
            block ({}, playing);
    }

    Bytes save()
    {
        juce::MemoryBlock m;
        p.getStateInformation (m);
        const auto* b = static_cast<const std::uint8_t*> (m.getData());
        return Bytes (b, b + m.getSize());
    }

    void load (const Bytes& b) { p.setStateInformation (b.data(), (int) b.size()); }

    std::vector<double> values()
    {
        std::vector<double> v;
        for (int i = 0; i < kNumParams; ++i)
            v.push_back (value (i));
        return v;
    }

    std::string read (const char* key)
    {
        std::string text (SC_STATE_MAX, '\0');
        const int n = sc_shell_read (p.engine(), key, text.data(), (int) text.size());
        text.resize (n > 0 ? (std::size_t) n : 0);
        return text;
    }
};

juce::MidiBuffer message (const juce::MidiMessage& m, int at = 0)
{
    juce::MidiBuffer b;
    b.addEvent (m, at);
    return b;
}

float lowest (const juce::AudioBuffer<float>& b, int ch = 0)
{
    return juce::FloatVectorOperations::findMinimum (b.getReadPointer (ch), b.getNumSamples());
}

float highest (const juce::AudioBuffer<float>& b, int ch = 0)
{
    return juce::FloatVectorOperations::findMaximum (b.getReadPointer (ch), b.getNumSamples());
}

/* A MIDI-triggered instance holding its gate: Gate mode, the trigger note
 * held, the duck at its floor. */
void holdGate (Instance& a, int channel = 1)
{
    a.set (kSource, 1.0);
    a.set (kMidiMode, 1.0);
    a.block (message (juce::MidiMessage::noteOn (channel, 36, (juce::uint8) 100)));
    a.play (20);
    REQUIRE (highest (a.block()) < 0.05f);
}
} // namespace

/* ------------------------------------------------------------ parameters -- */

TEST_CASE ("the fifteen and Bypass, as the iPlug2 build reported them")
{
    Instance a;
    const auto doc = juce::JSON::parse (fixtures.getChildFile ("parameters.json"));
    const auto defaults = juce::JSON::parse (fixtures.getChildFile ("default.json"));
    std::map<int, juce::String> text;
    for (const auto& v : *defaults["values"].getArray())
        text[(int) v["id"]] = v["display"].toString();

    int count = 0, midi = 0;
    for (const auto& row : *doc["parameters"].getArray())
    {
        const int id = row["id"];
        CAPTURE (id);
        /* iPlug2's MIDI-CC parameters, 65538 on: JUCE maps CCs onto
         * parameters of its own (docs/live.md says so). */
        if (id >= 65538)
        {
            CHECK ((int) row["flags"] == 0);
            ++midi;
            continue;
        }
        auto* param = id == 65536 ? a.p.getBypassParameter() : a.p.getParameters()[id];
        REQUIRE (param != nullptr);
        ++count;
        CHECK (param->getName (128) == row["title"].toString());
        CHECK (param->getLabel() == row["units"].toString());
        CHECK ((param->isDiscrete() ? param->getNumSteps() - 1 : 0) == (int) row["stepCount"]);
        CHECK (param->getDefaultValue() == doctest::Approx ((double) row["defaultNormalizedValue"]).epsilon (1.0e-6));
        CHECK (param->getText (param->getDefaultValue(), 128) == text[id]);
    }
    CHECK (count == 16);
    CHECK (midi == 130);
    /* With legacy IDs the index is the VST3 ID: Bypass is the last, 15. */
    CHECK (a.p.getBypassParameter() == a.p.getParameters()[kNumParams]);
    CHECK (a.p.getBypassParameter()->isBoolean());
    CHECK (a.p.acceptsMidi());
    CHECK_FALSE (a.p.producesMidi());
}

TEST_CASE ("Rate is the engine's table, its default the engine's")
{
    CHECK (sc_core_rate_default() == defaultRate);
    char label[32];
    CHECK (sc_core_rate_label (numRates - 1, label, (int) sizeof label) > 0);
    CHECK (sc_core_rate_label (numRates, label, (int) sizeof label) < 0);
    Instance a;
    CHECK (a.p.parameter (kRate).getAllValueStrings()[defaultRate] == "1/4");
    CHECK (a.p.parameter (kNote).getAllValueStrings()[60] == "C3");
    CHECK (a.p.parameter (kNote).getAllValueStrings()[0] == "C-2");
    CHECK (a.p.parameter (kNote).getAllValueStrings()[127] == "G8");
}

TEST_CASE ("every value a host can hold reads back from its own text")
{
    Instance a;
    for (int i = 0; i < kNumParams; ++i)
    {
        auto& p = a.p.parameter (i);
        const int steps = p.isDiscrete() ? p.getNumSteps() : 101;
        for (int k = 0; k < steps; ++k)
        {
            const float v = (float) k / (float) (steps - 1);
            CAPTURE (i);
            CAPTURE (v);
            const auto shown = p.getText (v, 128);
            CHECK (p.getText (p.getValueForText (shown), 128) == shown);
        }
    }
    /* A value typed with or without its unit; what is not a number leaves
     * the value where it was; Threshold's bottom reads back as the bottom. */
    auto& depth = a.p.parameter (kDepth);
    CHECK (depth.getValueForText ("40") == doctest::Approx (0.4f));
    CHECK (depth.getValueForText ("40.0 %") == doctest::Approx (0.4f));
    depth.setPlain (25.0);
    CHECK (depth.getValueForText ("loud") == doctest::Approx (0.25f));
    auto& threshold = a.p.parameter (kThreshold);
    CHECK (threshold.getText (0.0f, 128) == "-inf dB");
    CHECK (threshold.getValueForText ("-inf dB") == 0.0f);
    CHECK (threshold.getValueForText ("-12") == doctest::Approx (0.8f));
    CHECK (a.p.parameter (kDelay).getText (0.4f, 128) == "-20.0 %");
    CHECK (a.p.parameter (kLockout).getText (0.25f, 128) == "50 ms");
    CHECK (a.p.parameter (kRate).getValueForText ("1/8") == doctest::Approx (6.0f / 11.0f));
    CHECK (a.p.parameter (kNote).getValueForText ("C3") == doctest::Approx (60.0f / 127.0f));
    CHECK (a.p.parameter (kChannel).getValueForText ("Omni") == 0.0f);
    CHECK (a.p.parameter (kChannel).getValueForText ("10") == doctest::Approx (10.0f / 16.0f));
    CHECK (a.p.parameter (kMidiMode).getValueForText ("Gate") == 1.0f);
}

/* ------------------------------------------------------------- fixtures -- */

TEST_CASE ("every iPlug2 fixture loads to its exact values, displays and bypass, and saves back byte for byte")
{
    for (const char* scenario : { "default", "custom" })
    {
        CAPTURE (scenario);
        const auto doc = juce::JSON::parse (fixtures.getChildFile (juce::String (scenario) + ".json"));
        const auto bytes = fixture (scenario);
        const auto decoded = ni::nist::read (layout(), bytes.data(), bytes.size());
        REQUIRE (decoded.has_value());

        Instance a;
        a.load (bytes);
        for (int i = 0; i < kNumParams; ++i)
        {
            CAPTURE (i);
            CHECK (a.value (i) == decoded->params[(std::size_t) i]);
        }
        CHECK ((a.p.getBypassParameter()->getValue() >= 0.5f) == decoded->bypass.value_or (false));
        for (const auto& v : *doc["values"].getArray())
        {
            const int id = v["id"];
            auto* param = id == 65536 ? a.p.getBypassParameter() : a.p.getParameters()[id];
            CAPTURE (id);
            CHECK (param->getValue() == doctest::Approx ((double) v["normalized"]).epsilon (1.0e-6));
            CHECK (param->getText (param->getValue(), 128) == v["display"].toString());
        }
        /* The same chunk the iPlug2 build wrote, which it reads as its own. */
        CHECK (a.save() == bytes);
        a.play (2);
        CHECK (a.save() == bytes);
    }
}

/* ---------------------------------------------------------------- state -- */

TEST_CASE ("random values: save, a fresh instance, load, save again")
{
    std::mt19937 rng (20261007);
    for (int round = 0; round < 10; ++round)
    {
        Instance a;
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& s = specOf (i);
            std::uniform_real_distribution<double> d (s.min, s.max);
            a.p.parameter (i).setPlain (d (rng));
        }
        a.play (1);
        const auto first = a.save();

        Instance b;
        b.load (first);
        CHECK (b.values() == a.values());
        CHECK (b.save() == first);
    }
}

TEST_CASE ("what no build wrote is refused, and changes nothing")
{
    Instance b;
    b.set (kDepth, 42.0);
    const auto before = b.values();
    std::mt19937 rng (7);
    for (int round = 0; round < 3; ++round)
    {
        Bytes noise (4096);
        for (auto& x : noise)
            x = (std::uint8_t) rng();
        b.load (noise);
        CHECK (b.values() == before);
    }
    b.load ({});
    CHECK (b.values() == before);
    /* A stepped value that is no whole number is no value a build wrote. */
    auto halfRate = fixture ("default");
    const double half = 2.5;
    std::memcpy (halfRate.data() + 16 + 8 * kRate, &half, 8);
    b.load (halfRate);
    CHECK (b.values() == before);
}

TEST_CASE ("a headerless chunk from before 2026-09-30 opens, and the bypass a chunk carries is restored")
{
    const auto decoded = ni::nist::read (layout(), fixture ("custom").data(), fixture ("custom").size());
    Bytes legacy;
    for (const double v : decoded->params)
    {
        const auto* b = reinterpret_cast<const std::uint8_t*> (&v);
        legacy.insert (legacy.end(), b, b + 8);
    }
    const std::int32_t on = 1;
    const auto* b = reinterpret_cast<const std::uint8_t*> (&on);
    legacy.insert (legacy.end(), b, b + 4);

    Instance a;
    a.load (legacy);
    CHECK (a.values() == decoded->params);
    /* iPlug2 put this into its controller alone; the processor ran on. */
    CHECK (a.p.getBypassParameter()->getValue() == 1.0f);
    /* And it saves the current form, which every iPlug2 build reads. */
    const auto saved = a.save();
    const auto again = ni::nist::read (layout(), saved.data(), saved.size());
    REQUIRE (again.has_value());
    CHECK_FALSE (again->legacy);
    CHECK (again->bypass == std::optional<bool> (true));
}

/* ---------------------------------------------------------------- audio -- */

TEST_CASE ("the host's values reach the engine, Depth and Vel as fractions")
{
    Instance a;
    a.load (fixture ("custom"));
    a.play (4);
    const auto f = [] (const std::string& text)
    {
        std::vector<double> out;
        std::size_t at = 0;
        while (at <= text.size())
        {
            const auto end = std::min (text.find (':', at), text.size());
            out.push_back (std::stod (text.substr (at, end - at)));
            at = end + 1;
        }
        return out;
    } (a.read ("params"));
    REQUIRE (f.size() == (std::size_t) kNumParams);
    for (int i = 0; i < kNumParams; ++i)
    {
        CAPTURE (i);
        CHECK (f[(std::size_t) i] == doctest::Approx (toEngine (i, a.value (i))).epsilon (1.0e-6));
    }
    CHECK (f[kDepth] == doctest::Approx (0.75));
    CHECK (f[kVelSens] == doctest::Approx (0.4));
}

TEST_CASE ("Cycle ducks under a running transport, both channels alike, and rests when it stops")
{
    Instance a;
    float lo = 1.0f, hi = 0.0f;
    bool alike = true;
    for (int i = 0; i < 100; ++i)
    {
        const auto b = a.block();
        lo = std::min (lo, lowest (b));
        hi = std::max (hi, highest (b));
        for (int s = 0; s < b.getNumSamples(); ++s)
            alike = alike && juce::exactlyEqual (b.getSample (0, s), b.getSample (1, s));
    }
    CHECK (lo < 0.05f);
    CHECK (hi > 0.45f);
    CHECK (alike);

    /* No transport: a stopped Cycle releases and rests open. */
    a.play (40, false);
    const auto b = a.block ({}, false);
    CHECK (lowest (b) == 0.5f);
}

TEST_CASE ("a mono track is ducked on its one channel")
{
    Instance a (juce::AudioChannelSet::disabled(), juce::AudioChannelSet::mono());
    REQUIRE (a.p.getMainBusNumInputChannels() == 1);
    float lo = 1.0f;
    for (int i = 0; i < 100; ++i)
        lo = std::min (lo, lowest (a.block()));
    CHECK (lo < 0.05f);
}

TEST_CASE ("a key ducks the track on Sidechain, and the key bus is reported as the host left it")
{
    for (const auto& key : { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono() })
    {
        CAPTURE (key.getDescription());
        Instance a (key);
        a.set (kSource, 2.0);
        a.play (2);
        CHECK (a.model().buses().keyConnected);
        CHECK (a.model().state().source == Source::sidechain);
        const auto before = a.model().state().fires;

        /* A kick on the key every 9600 samples, well above -24 dB. */
        float lo = 1.0f;
        for (int i = 0; i < 60; ++i)
        {
            auto b = a.buffer();
            for (int ch = a.p.getMainBusNumInputChannels(); ch < b.getNumChannels(); ++ch)
                for (int s = 0; s < blockSize; ++s)
                    b.setSample (ch, s, ((i * blockSize + s) % 9600) < 480 ? 0.8f : 0.0f);
            juce::MidiBuffer none;
            a.p.processBlock (b, none);
            lo = std::min (lo, lowest (b));
        }
        CHECK (lo < 0.05f);
        CHECK (a.model().state().fires > before);
    }

    Instance unkeyed;
    unkeyed.set (kSource, 2.0);
    unkeyed.play (2);
    CHECK_FALSE (unkeyed.model().buses().keyConnected);
    /* No key, no trigger: the track passes as it came. */
    CHECK (lowest (unkeyed.block()) == 0.5f);
}

TEST_CASE ("the buses the iPlug2 build offered, and no others")
{
    Processor p;
    using Set = juce::AudioChannelSet;
    const auto layout = [] (Set main, Set out, Set key)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (main);
        l.inputBuses.add (key);
        l.outputBuses.add (out);
        return l;
    };
    CHECK (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo(), Set::disabled())));
    CHECK (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo(), Set::stereo())));
    CHECK (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo(), Set::mono())));
    CHECK (p.checkBusesLayoutSupported (layout (Set::mono(), Set::mono(), Set::mono())));
    CHECK (p.checkBusesLayoutSupported (layout (Set::mono(), Set::mono(), Set::disabled())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::mono(), Set::stereo(), Set::disabled())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::create5point1(), Set::create5point1(), Set::disabled())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo(), Set::create5point1())));
}

/* ----------------------------------------------------------------- MIDI -- */

TEST_CASE ("a trigger note ducks from its own sample, not the top of the block")
{
    Instance a;
    a.set (kSource, 1.0);
    a.play (4);
    const auto before = a.model().state().fires;
    const auto b = a.block (message (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 300));
    for (int s = 0; s < 300; ++s)
        REQUIRE (b.getSample (0, s) == 0.5f);
    CHECK (b.getSample (0, blockSize - 1) < 0.45f);
    a.play (2);
    CHECK (a.model().state().fires == before + 1);

    /* Another note, or the note on another channel, is no trigger. */
    Instance c;
    c.set (kSource, 1.0);
    c.play (4);
    c.block (message (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100)));
    c.block (message (juce::MidiMessage::noteOn (2, 36, (juce::uint8) 100)));
    CHECK (lowest (c.block()) == 0.5f);
}

TEST_CASE ("the panic: CC 120 and CC 123 open a held gate, on the trigger's channel or any with Omni")
{
    for (const int cc : { 120, 123 })
        for (const bool omni : { false, true })
        {
            CAPTURE (cc);
            CAPTURE (omni);
            Instance a;
            a.set (kChannel, omni ? 0.0 : 1.0);
            holdGate (a, omni ? 7 : 1);
            a.block (message (juce::MidiMessage::controllerEvent (omni ? 16 : 1, cc, 0)));
            const auto b = a.block();
            CHECK (lowest (b) > 0.45f);
        }

    /* A channel mode message speaks for its own channel: one on a channel the
     * trigger does not listen to is not this plugin's panic. */
    Instance other;
    holdGate (other);
    other.block (message (juce::MidiMessage::controllerEvent (2, 123, 0)));
    CHECK (highest (other.block()) < 0.05f);

    /* Without one, the gate holds as long as the note does. */
    Instance held;
    holdGate (held);
    held.play (20);
    CHECK (highest (held.block()) < 0.05f);
}

TEST_CASE ("bypassed, the audio passes untouched while the engine still hears the MIDI")
{
    Instance a;
    holdGate (a);
    const auto through = a.block (message (juce::MidiMessage::noteOff (1, 36)), true, true);
    CHECK (lowest (through) == 0.5f);
    CHECK (highest (through) == 0.5f);
    for (int i = 0; i < 20; ++i)
        CHECK (lowest (a.block ({}, true, true)) == 0.5f);
    /* The note was released while bypassed; back in, the duck is gone. */
    CHECK (lowest (a.block()) > 0.45f);
}

/* ---------------------------------------------------------------- model -- */

TEST_CASE ("the model's state and stage lengths are the engine's readouts")
{
    Instance a;
    auto& m = a.model();
    a.play (10);
    const auto s = m.state();
    CHECK (s.source == Source::cycle);
    CHECK (s.rate == defaultRate);
    CHECK (s.msPerCycle == doctest::Approx (500.0));
    CHECK (s.advancing);
    CHECK (s.fires >= 1);
    CHECK (s.sweep >= 0.0);
    CHECK (s.sweep <= 1.0);

    const auto ms = m.stageMs();
    CHECK (ms.delay == doctest::Approx (0.0));
    CHECK (ms.attack == doctest::Approx (10.0));
    CHECK (ms.hold == doctest::Approx (40.0));
    CHECK (ms.release == doctest::Approx (175.0));

    a.set (kRate, 6.0); /* 1/8 */
    a.set (kDelay, -20.0);
    a.play (4);
    CHECK (m.state().rate == 6);
    CHECK (m.state().msPerCycle == doctest::Approx (250.0));
    CHECK (m.stageMs().delay == doctest::Approx (-50.0));

    /* The transport stops: the cycle stops advancing. */
    a.play (10, false);
    CHECK_FALSE (m.state().advancing);
}

TEST_CASE ("the model's shape and marks are the engine's single shot, from the host's values now")
{
    Instance a;
    auto& m = a.model();
    auto marks = m.shapeMarks();
    CHECK (marks.start == doctest::Approx (0.0));
    CHECK (marks.bottom == doctest::Approx (2.0));
    CHECK (marks.holdEnd == doctest::Approx (10.0));
    CHECK (marks.end == doctest::Approx (45.0));
    CHECK (marks.span == doctest::Approx (45.0));
    CHECK (marks.floor == doctest::Approx (0.0));

    /* No block in between: a dragged handle is under the pointer at once. */
    a.set (kDelay, -20.0);
    a.set (kDepth, 75.0);
    marks = m.shapeMarks();
    CHECK (marks.start == doctest::Approx (80.0));
    CHECK (marks.end == doctest::Approx (25.0));
    CHECK (marks.floor == doctest::Approx (0.25));
    std::vector<float> gain (101);
    m.shapeGain (gain.data(), (int) gain.size());
    CHECK (*std::min_element (gain.begin(), gain.end()) == doctest::Approx (0.25f).epsilon (0.01));
    CHECK (*std::max_element (gain.begin(), gain.end()) == doctest::Approx (1.0f));

    /* Elsewhere a negative Delay is no wait: the engine's own clamp. */
    a.set (kSource, 1.0);
    CHECK (m.shapeMarks().start == doctest::Approx (0.0));
}

TEST_CASE ("the shape drawn is the duck the engine plays, phase for phase")
{
    Instance a;
    a.set (kAttack, 12.0);
    a.set (kHold, 20.0);
    a.set (kRelease, 40.0);
    a.set (kDepth, 80.0);
    a.set (kDelay, 10.0);
    /* Three cycles to lock and settle, then one cycle measured: at 120 BPM
     * and 1/4 a cycle is 24000 samples from a beat. */
    std::vector<float> played;
    for (int i = 0; i < 4 * 24000 / blockSize + 2; ++i)
    {
        const auto start = a.head.sample;
        const auto b = a.block();
        for (int s = 0; s < blockSize; ++s)
            if (start + s >= 3 * 24000 && start + s < 4 * 24000)
                played.push_back (b.getSample (0, s) / 0.5f);
    }
    REQUIRE (played.size() == 24000);
    constexpr int points = 101;
    std::vector<float> drawn (points);
    a.model().shapeGain (drawn.data(), points);
    float worst = 0.0f;
    for (int k = 0; k < points - 1; ++k)
        worst = std::max (worst, std::abs (drawn[(std::size_t) k] - played[(std::size_t) (k * 240)]));
    CHECK (worst < 0.02f);
}

TEST_CASE ("the model's capture moves with the host's, only while a window is open")
{
    Instance a;
    auto& m = a.model();
    a.play (20);
    CHECK (m.scope().count == 0);

    a.p.editorOpened();
    a.play (60);
    const auto& s = m.scope();
    REQUIRE (s.count == Processor::scopeColumns);
    int seen = 0;
    float deepest = 1.0f;
    for (int c = 0; c < s.count; ++c)
        if (s.isSeen (c))
        {
            ++seen;
            deepest = std::min (deepest, s.at (c, Scope::gain));
            CHECK (s.at (c, Scope::dryHi) == doctest::Approx (0.5f));
        }
    CHECK (seen == s.count);
    CHECK (deepest < 0.05f);

    /* Closed, it stands still. */
    a.p.editorClosed();
    const auto before = m.scope().data;
    a.play (20);
    CHECK (m.scope().data == before);
}

TEST_CASE ("the Ground rings with the host's beat, only while a window is open")
{
    Instance a;
    float rings[16];
    a.play (190); /* two seconds, four beats, with no window */
    a.p.editorOpened();
    CHECK (a.model().takeRings (rings, 16) == 0);
    a.play (94); /* a second: two beats */
    const int n = a.model().takeRings (rings, 16);
    CHECK (n >= 1);
    CHECK (n <= 3);
    /* Bypassed, it keeps the host's time. */
    for (int i = 0; i < 94; ++i)
        a.block ({}, true, true);
    CHECK (a.model().takeRings (rings, 16) >= 1);
    a.p.editorClosed();
}

/* --------------------------------------------------------------- editor -- */

TEST_CASE ("the editor on the real model: the trigger's state, and a key nobody routed")
{
    Instance a;
    a.play (10);
    double now = 0.0;
    SideChainEditor e (a.model(), [&now] { return now; });
    e.tick (now += 16.0);
    CHECK (e.stateText().startsWith ("Cycle 1/4"));
    CHECK (e.warningText().isEmpty());

    a.set (kSource, 2.0);
    a.play (2);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    e.tick (now += 16.0);
    CHECK (e.stateText().startsWith ("Sidechain"));
    CHECK (e.warningText() == "no key routed");

    Instance keyed (juce::AudioChannelSet::stereo());
    keyed.set (kSource, 2.0);
    keyed.play (2);
    SideChainEditor f (keyed.model(), [&now] { return now; });
    f.tick (now += 16.0);
    CHECK (f.warningText() != "no key routed");
}

TEST_CASE ("the plugin's window is the design's, and stays whole while a set loads on another thread")
{
    Instance a;
    a.play (2);
    std::unique_ptr<juce::AudioProcessorEditor> window (a.p.createEditorAndMakeActive());
    REQUIRE (window != nullptr);
    CHECK (window->getWidth() == SideChainEditor::designWidth);
    CHECK (window->getHeight() == SideChainEditor::designHeight);
    a.play (20);
    CHECK (a.model().scope().count == Processor::scopeColumns);

    const auto custom = fixture ("custom");
    std::thread host ([&] { a.p.setStateInformation (custom.data(), (int) custom.size()); });
    host.join();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    a.play (2);
    auto* design = dynamic_cast<SideChainEditor*> (&dynamic_cast<ni::PluginEditor&> (*window).getDesign());
    REQUIRE (design != nullptr);
    design->tick (1000.0);
    CHECK (design->knob (kDepth).binding().value() == doctest::Approx (a.p.parameter (kDepth).getValue()));
    CHECK (a.value (kDepth) == 75.0);
    CHECK (design->stateText().startsWith ("Sidechain"));

    window.reset();
    /* Closed: the capture stops with it. */
    const auto before = a.model().scope().data;
    a.play (20);
    CHECK (a.model().scope().data == before);
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    doctest::Context context (argc, argv);
    return context.run();
}
