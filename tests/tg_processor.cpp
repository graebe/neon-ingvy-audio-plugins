// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate on the JUCE shell, its processor in the program: the
 * parameters a host sees, the state a set keeps, the slots, the editor's
 * model on the real engine, and the native editor driven by that model.
 *
 *   the parameters  as the iPlug2 build reported them (parameters.json):
 *                   names, units, steps, defaults and default texts
 *   the fixtures    every tests/fixtures/iplug2/NITranceGate state loads to
 *                   its exact parameters, its blob and its bypass, and saves
 *                   back -- byte for byte where the engine's blob is the same
 *   the state       save, a fresh instance, load, save again: the same bytes,
 *                   random values and a drawn pattern included; what no build
 *                   wrote changes nothing; a headerless chunk from the
 *                   twelve-parameter builds opens
 *   the slots       a switch recalls a sound and the host follows; automation
 *                   writes the current slot alone; a save straight after a
 *                   switch keeps the slot switched to
 *   the model       snapshots from the engine (the pattern, its fade levels,
 *                   the detents, the stages' text, the transport, the curves,
 *                   the capture, the Ground's rings) and commands into it
 *                   (steps, files, pastes) -- each of which marks the set
 *                   unsaved, as no load or automation does
 *   the editor      on the real model: the window, its rows growing with
 *                   Length, the ring's playhead moving under a running
 *                   transport, a set loaded on another thread while it is
 *                   open, and the window verbs through a clipboard and panels
 *
 * What is the editor's own behaviour against a model that does as told is
 * tests/ui's (trance-gate_*.cpp, on fakes); this is what only the real model
 * can show. tests/tg_host.cpp holds the built VST3, as a DAW hosts it.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "EngineModel.h"
#include "TranceGate.h"
#include "Window.h"
#include "trance-gate_fakes.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cmath>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace ni::tg;
using Bytes = std::vector<std::uint8_t>;

namespace
{
const juce::File fixtures = juce::File (NI_FIXTURES).getChildFile ("NITranceGate");

Bytes fixture (const juce::String& scenario)
{
    juce::MemoryBlock b;
    REQUIRE (fixtures.getChildFile (scenario + ".component.bin").loadFileAsData (b));
    const auto* p = static_cast<const std::uint8_t*> (b.getData());
    return Bytes (p, p + b.getSize());
}

/* A transport playing at 120 BPM in 4/4 from bar 1, moved on by the caller. */
struct Playhead final : juce::AudioPlayHead
{
    double rate = 48000.0;
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

/* What the host is told: every set marked unsaved, and every parameter the
 * plugin moved. */
struct HostEar final : juce::AudioProcessorListener
{
    int dirty = 0;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override
    {
        if (d.nonParameterStateChanged)
            ++dirty;
    }
};

/* One instance, as a host runs it. */
struct Instance
{
    static constexpr int frames = 64;

    Processor p;
    Playhead head;
    HostEar ear;

    Instance()
    {
        p.prepareToPlay (48000.0, 512);
        p.addListener (&ear);
    }
    ~Instance() { p.removeListener (&ear); }

    EngineModel& model() { return p.model(); }
    double value (int i) { return p.parameter (i).plain(); }

    /* One audio block of silence; with the transport when `playing`. */
    void block (bool playing = false)
    {
        p.setPlayHead (playing ? &head : nullptr);
        juce::AudioBuffer<float> buffer (2, frames);
        buffer.clear();
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);
        head.sample += frames;
        p.setPlayHead (nullptr);
    }

    /* A block of a steady input under the running transport. */
    void play (int blocks)
    {
        p.setPlayHead (&head);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (ch, i, 0.5f);
            p.processBlock (buffer, midi);
            head.sample += 512;
        }
        p.setPlayHead (nullptr);
    }

    bool follow() { return p.followEngine(); }

    /* The host moves one parameter, a block pushes it, the host follows. */
    void automate (int i, double v)
    {
        p.parameter (i).setPlainNotifyingHost (v);
        block();
        follow();
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
        std::string text (TG_STATE_MAX, '\0');
        const int n = tg_shell_read (p.engine(), key, text.data(), (int) text.size());
        text.resize (n > 0 ? (std::size_t) n : 0);
        return text;
    }
};

/* A slot's whole sound, as the host would hold it: one value per parameter
 * but Slot, apart from the defaults and from the other slots'. */
std::vector<std::pair<int, double>> sound (int k)
{
    return { { kLength, 5.0 + k },  { kRate, 3.0 + k },     { kLegato, 1.0 },      { kTimeMode, 1.0 },
             { kCurve, (double) ((1 + k) % 3) }, { kAmount, 40.0 + k },  { kWidth, 60.0 + k },
             { kAttack, 20.0 + k }, { kDecay, 30.0 + k },  { kSustain, 50.0 + k }, { kRelease, 70.0 + k },
             { kFade, 80.0 + k },   { kFadeSoft, 1.0 },    { kFadeDir, 1.0 } };
}

void checkSound (Instance& a, int k)
{
    for (const auto& row : sound (k))
    {
        const int i = row.first;
        CAPTURE (i);
        CHECK (sameInEngine (i, a.value (i), toEngine (i, row.second)));
    }
}

/* Edits of every kind the editor makes, none of them a host parameter. */
void edit (EngineModel& m)
{
    m.setStep (1, StepMode::on);
    m.setStep (2, StepMode::tie);
    m.setStep (4, StepMode::off);
    m.setDepth (2, 0.5f);
    m.setOrder (1, 3);
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

    int count = 0;
    for (const auto& row : *doc["parameters"].getArray())
    {
        const int id = row["id"];
        CAPTURE (id);
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
    /* With legacy IDs the index is the VST3 ID: Bypass is the last, 15. */
    CHECK (a.p.getBypassParameter() == a.p.getParameters()[kNumParams]);
    CHECK (a.p.getBypassParameter()->isBoolean());
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
    /* A percentage typed with or without its unit; what is not a number
     * leaves the value where it was. */
    auto& amount = a.p.parameter (kAmount);
    CHECK (amount.getValueForText ("40") == doctest::Approx (0.4f));
    CHECK (amount.getValueForText ("40.00 %") == doctest::Approx (0.4f));
    amount.setPlain (25.0);
    CHECK (amount.getValueForText ("loud") == doctest::Approx (0.25f));
    CHECK (a.p.parameter (kRate).getValueForText ("1/8") == doctest::Approx (5.0f / 12.0f));
    CHECK (a.p.parameter (kLegato).getValueForText ("On") == 1.0f);
}

/* ------------------------------------------------------------- fixtures -- */

TEST_CASE ("every iPlug2 fixture loads to its exact parameters, blob and bypass")
{
    for (const char* scenario : { "default", "slots", "bypassed" })
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
        /* What a host reads back for each, as the iPlug2 build answered. */
        for (const auto& v : *doc["values"].getArray())
        {
            const int id = v["id"];
            auto* param = id == 65536 ? a.p.getBypassParameter() : a.p.getParameters()[id];
            CAPTURE (id);
            CHECK (param->getValue() == doctest::Approx ((double) v["normalized"]).epsilon (1.0e-6));
            CHECK (param->getText (param->getValue(), 128) == v["display"].toString());
        }
        /* The engine holds the blob: every slot's pattern and sound. */
        CHECK (a.read ("state") == decoded->strings.front());
        a.block();
        CHECK (a.read ("state") == decoded->strings.front());
        CHECK_FALSE (a.follow());

        /* And it saves what it loaded, byte for byte -- the same chunk the
         * iPlug2 build wrote, which that build reads as its own. */
        CHECK (a.save() == bytes);
    }
}

TEST_CASE ("a set from the twelve-parameter builds opens, the fade at its defaults")
{
    const auto bytes = fixture ("slots");
    const auto decoded = ni::nist::read (layout(), bytes.data(), bytes.size());
    Bytes legacy;
    const auto put = [&legacy] (const void* p, std::size_t n)
    {
        const auto* b = static_cast<const std::uint8_t*> (p);
        legacy.insert (legacy.end(), b, b + n);
    };
    for (int i = 0; i < 12; ++i)
        put (&decoded->params[(std::size_t) i], 8);
    const auto& blob = decoded->strings.front();
    const auto len = (std::int32_t) blob.size();
    put (&len, 4);
    put (blob.data(), blob.size());

    Instance a;
    a.load (legacy);
    for (int i = 0; i < 12; ++i)
        CHECK (a.value (i) == decoded->params[(std::size_t) i]);
    CHECK (a.value (kFade) == 100.0);
    CHECK (a.value (kFadeSoft) == 0.0);
    CHECK (a.value (kFadeDir) == 0.0);
    CHECK (a.read ("state").find ("\"p1\":\"54A954A9") != std::string::npos);
}

/* ---------------------------------------------------------------- state -- */

TEST_CASE ("random values and a drawn pattern: save, a fresh instance, load, save again")
{
    std::mt19937 rng (20261007);
    for (int round = 0; round < 10; ++round)
    {
        Instance a;
        for (int i = 1; i < kNumParams; ++i)
        {
            const auto& s = specOf (i);
            std::uniform_real_distribution<double> d (s.min, s.max);
            a.p.parameter (i).setPlain (d (rng));
        }
        a.block();
        edit (a.model());
        const auto first = a.save();

        Instance b;
        b.load (first);
        for (int i = 0; i < kNumParams; ++i)
        {
            CAPTURE (i);
            CHECK (b.value (i) == a.value (i));
        }
        CHECK (b.save() == first);
    }
}

TEST_CASE ("an edited pattern survives a save made before any audio ran")
{
    Instance a;
    const auto fresh = a.read ("state");
    edit (a.model());
    a.model().randomize();
    const auto saved = a.save();
    Instance b;
    b.load (saved);
    CHECK (b.read ("state") == a.read ("state"));
    CHECK (b.read ("state") != fresh);
    a.block();
    CHECK (a.read ("state") == b.read ("state"));
}

TEST_CASE ("what no build wrote is refused, and changes nothing")
{
    Instance b;
    b.automate (kAmount, 42.0);
    const auto before = b.values();
    const auto state = b.read ("state");
    std::mt19937 rng (7);
    for (int round = 0; round < 3; ++round)
    {
        Bytes noise (256 * 1024);
        for (auto& x : noise)
            x = (std::uint8_t) rng();
        b.load (noise);
        CHECK (b.values() == before);
        CHECK (b.read ("state") == state);
    }
    b.load ({});
    CHECK (b.values() == before);
}

/* ---------------------------------------------------------------- slots -- */

TEST_CASE ("a slot switch moves every host parameter to the new slot's values")
{
    Instance a;
    a.block();
    for (const auto& [i, v] : sound (0))
        a.automate (i, v);
    const auto one = a.values();

    a.automate (kSlot, 2.0);
    for (int i = 1; i < kNumParams; ++i)
    {
        CAPTURE (i);
        CHECK (sameInEngine (i, a.value (i), toEngine (i, specOf (i).def)));
    }
    for (const auto& [i, v] : sound (1))
        a.automate (i, v);
    a.automate (kSlot, 1.0);
    for (int i = 1; i < kNumParams; ++i)
        CHECK (sameInEngine (i, a.value (i), toEngine (i, one[(std::size_t) i])));
    a.automate (kSlot, 2.0);
    checkSound (a, 1);
}

TEST_CASE ("host automation writes the current slot and no other; nothing to follow is not a switch")
{
    Instance a;
    a.block();
    a.automate (kSlot, 3.0);
    a.automate (kAmount, 25.0);
    a.automate (kRate, 2.0);
    CHECK_FALSE (a.follow());
    a.automate (kSlot, 4.0);
    CHECK (a.value (kAmount) == 100.0);
    CHECK (a.value (kRate) == (double) tg_core_rate_default());
    a.automate (kSlot, 3.0);
    CHECK (a.value (kAmount) == 25.0);
    CHECK (a.value (kRate) == 2.0);
}

TEST_CASE ("every slot's sound survives a save and a reload")
{
    Instance a;
    a.block();
    for (int k = 0; k < 3; ++k)
    {
        a.automate (kSlot, 1.0 + k);
        for (const auto& [i, v] : sound (k))
            a.automate (i, v);
    }
    a.automate (kSlot, 2.0);
    const auto first = a.save();

    Instance b;
    b.load (first);
    CHECK (b.values() == a.values());
    b.block();
    CHECK_FALSE (b.follow());
    CHECK (b.save() == first);
    for (const int k : { 0, 2, 1 })
    {
        b.automate (kSlot, 1.0 + k);
        checkSound (b, k);
    }
}

TEST_CASE ("a save straight after a switch keeps each slot's own sound")
{
    /* The host moves Slot and saves before a block applies it -- the
     * host's audio engine off. The saved values are the new slot's, and the
     * set reopens with slot 1's sound in slot 1 and slot 2's in slot 2. */
    Instance a;
    a.block();
    for (const auto& [i, v] : sound (0))
        a.automate (i, v);
    a.p.parameter (kSlot).setPlainNotifyingHost (2.0);
    const auto saved = a.save();

    Instance b;
    b.load (saved);
    b.block();
    CHECK (b.value (kSlot) == 2.0);
    CHECK (b.value (kAmount) == 100.0);
    b.automate (kSlot, 1.0);
    checkSound (b, 0);
}

TEST_CASE ("an older project loads one sound into all eight slots")
{
    auto state = ni::nist::defaults (layout());
    state.params[kSlot] = 3.0;
    state.params[kAmount] = 70.0;
    state.params[kAttack] = 12.5;
    state.strings = { "{\"sv\":6,\"slot\":2,\"rate\":\"1/8\",\"attack\":12.50,\"amount\":0.700,"
                      "\"p0\":\"5555:0:16\",\"p2\":\"FFFF:0:8\"}" };
    const auto chunk = ni::nist::write (layout(), state);

    Instance b;
    b.block();
    b.automate (kSlot, 6.0);
    b.automate (kAmount, 12.0);
    b.load (chunk);
    b.block();
    b.follow();
    for (int k = 0; k < 8; ++k)
    {
        CAPTURE (k);
        b.automate (kSlot, 1.0 + k);
        CHECK (b.value (kAmount) == doctest::Approx (70.0));
        CHECK (b.value (kAttack) == doctest::Approx (12.5));
    }
}

/* ---------------------------------------------------------------- model -- */

TEST_CASE ("the model's pattern is the engine's, its fade levels included")
{
    Instance a;
    auto& m = a.model();
    const auto& fresh = m.pattern();
    CHECK (fresh.length == 16);
    m.setStep (3, StepMode::tie);
    m.setDepth (3, 0.5f);
    /* Queued: the snapshot is the engine as it will be, before any block. */
    CHECK (m.pattern().steps[3] == StepMode::tie);
    a.block();
    const auto& p = m.pattern();
    CHECK (p.steps[3] == StepMode::tie);
    CHECK (p.depths[3] == doctest::Approx (0.5f).epsilon (0.01));
    CHECK (p.cursor == 3);

    /* Fade 50 % hard: half the hits sound, every hole is a gap. */
    a.automate (kFade, 50.0);
    const auto& faded = m.pattern();
    int on = 0, sounding = 0;
    for (int i = 0; i < faded.length; ++i)
    {
        on += faded.steps[(std::size_t) i] != StepMode::off ? 1 : 0;
        sounding += faded.levels[(std::size_t) i] > 0.0f ? 1 : 0;
        if (faded.steps[(std::size_t) i] == StepMode::off)
            CHECK (faded.levels[(std::size_t) i] == 0.0f);
    }
    CHECK (sounding == on / 2);
    CHECK (std::all_of (faded.levels.begin() + faded.length, faded.levels.end(), [] (float v) { return v == 0.0f; }));
}

TEST_CASE ("the model's detents, stages and curves are the engine's")
{
    Instance a;
    a.block();
    auto& m = a.model();
    /* Half a bar to four bars at 1/16 in 4/4. */
    CHECK (m.lengthDetents() == std::vector<int> { 8, 16, 32, 64 });
    a.automate (kRate, 5.0); /* 1/8 */
    CHECK (m.lengthDetents() == std::vector<int> { 4, 8, 16, 32 });
    a.automate (kRate, 7.0);

    /* Env Time ms: 1.6 % of a 125 ms gate (1/16 at 120 BPM, Width 100 %). */
    CHECK (m.stageText (kAttack) == "2.0 ms");
    CHECK (m.stageValue (kAttack, "4 ms").value() == doctest::Approx (3.2f / 200.0f));
    CHECK (m.stageValue (kAttack, "25 %").value() == doctest::Approx (25.0f / 200.0f));
    CHECK_FALSE (m.stageValue (kAttack, "soon").has_value());
    a.automate (kTimeMode, 1.0);
    CHECK (m.stageText (kAttack) == "1.60 %");
    CHECK (m.stageValue (kAttack, "4").value() == doctest::Approx (4.0f / 200.0f));

    const auto gateSerial = m.gate().serial;
    const auto envSerial = m.envelope().serial;
    CHECK (m.gate().steps == 16);
    CHECK ((int) m.gate().values.size() == m.gate().steps * m.gate().perStep);
    CHECK ((int) m.envelope().gated.size() == m.envelope().steps * m.envelope().perStep);
    CHECK (m.envelope().ghost.size() == m.envelope().gated.size());
    CHECK (*std::max_element (m.gate().values.begin(), m.gate().values.end()) > 0.9f);
    /* The same patch renders nothing new; a new Width does. */
    CHECK (m.gate().serial == gateSerial);
    a.automate (kWidth, 40.0);
    CHECK (m.gate().serial != gateSerial);
    CHECK (m.envelope().serial != envSerial);
}

TEST_CASE ("the model's transport and capture move with the host's, while a window is open")
{
    Instance a;
    auto& m = a.model();
    CHECK_FALSE (m.transport().playing);
    a.p.editorOpened();
    a.play (20);
    const auto t = m.transport();
    CHECK (t.playing);
    CHECK (t.msPerStep == doctest::Approx (125.0));
    const double first = t.phase;
    const auto captured = m.capture().serial;
    CHECK (m.capture().columns == Processor::scopeColumns);
    a.play (10);
    CHECK (m.transport().phase != doctest::Approx (first));
    CHECK (m.capture().serial != captured);
    const bool heard = std::any_of (m.capture().data.begin(), m.capture().data.end(),
                                    [] (float v) { return v > 0.25f; });
    CHECK (heard);

    /* Closed, the capture stands still. */
    a.p.editorClosed();
    const auto stopped = m.capture().serial;
    a.play (10);
    CHECK (m.capture().serial == stopped);
}

TEST_CASE ("the Ground rings with the host's beat, only while a window is open")
{
    Instance a;
    float rings[16];
    a.play (200); /* two seconds, four beats, with no window */
    a.p.editorOpened();
    CHECK (a.model().takeRings (rings, 16) == 0);
    a.play (94); /* a second at 48 kHz: two beats */
    const int n = a.model().takeRings (rings, 16);
    CHECK (n >= 1);
    CHECK (n <= 3);
    a.p.editorClosed();
}

TEST_CASE ("the model's commands mark the set unsaved; a load and automation do not")
{
    Instance a;
    a.block();
    auto& m = a.model();
    a.automate (kAmount, 30.0);
    a.load (fixture ("slots"));
    CHECK (a.ear.dirty == 0);

    int expected = 0;
    m.setStep (0, StepMode::off);
    CHECK (a.ear.dirty == ++expected);
    m.setDepth (0, 0.25f);
    CHECK (a.ear.dirty == ++expected);
    m.setOrder (0, 2);
    CHECK (a.ear.dirty == ++expected);
    m.randomize();
    CHECK (a.ear.dirty == ++expected);
    m.shuffleOrder();
    CHECK (a.ear.dirty == ++expected);
    CHECK (m.paste (m.exportText (false)).kind == Transfer::Kind::slot);
    CHECK (a.ear.dirty == ++expected);
    CHECK (m.importText (m.exportText (true)).kind == Transfer::Kind::bank);
    CHECK (a.ear.dirty == ++expected);
    /* A refusal changes nothing, and marks nothing. */
    CHECK (m.paste ("https://example.com/not-a-slot").kind == Transfer::Kind::refused);
    CHECK (a.ear.dirty == expected);
}

TEST_CASE ("a slot exported from one instance imports into another's current slot, and its host follows")
{
    Instance a;
    a.block();
    for (const auto& [i, v] : sound (0))
        a.automate (i, v);
    const auto slot = a.model().exportText (false);
    CHECK (slot.find ("\"format\": \"ni-trance-gate-slot\"") != std::string::npos);

    Instance b;
    b.block();
    b.automate (kSlot, 5.0);
    const auto t = b.model().importText (slot);
    CHECK (t.kind == Transfer::Kind::slot);
    b.block();
    CHECK (b.follow());
    checkSound (b, 0);
    b.automate (kSlot, 1.0);
    CHECK (b.value (kAmount) == 100.0);

    /* A bank round-trips whole. */
    for (int k = 1; k < 3; ++k)
    {
        a.automate (kSlot, 1.0 + k);
        for (const auto& [i, v] : sound (k))
            a.automate (i, v);
    }
    const auto bank = a.model().exportText (true);
    Instance c;
    c.block();
    CHECK (c.model().importText (bank).kind == Transfer::Kind::bank);
    c.block();
    c.follow();
    CHECK (c.model().exportText (true) == bank);
}

TEST_CASE ("copy slot 1, paste into slot 2: slot 2 sounds like slot 1, and slot 1 is as it was")
{
    Instance a;
    a.block();
    for (const auto& [i, v] : sound (0))
        a.automate (i, v);
    const auto one = a.model().exportText (false);
    a.automate (kSlot, 2.0);
    CHECK (a.value (kAmount) == 100.0);
    CHECK (a.model().paste (one).kind == Transfer::Kind::slot);
    a.block();
    CHECK (a.follow());
    checkSound (a, 0);
    CHECK (a.model().exportText (false) == one);
    a.automate (kSlot, 1.0);
    checkSound (a, 0);
    a.automate (kSlot, 3.0);
    CHECK (a.value (kAmount) == 100.0);

    /* A whole patch -- what Copy wrote before slots, and the Move's. */
    Instance b;
    b.block();
    CHECK (b.model().paste (a.read ("state")).kind == Transfer::Kind::patch);
}

TEST_CASE ("what is not a Trance Gate slot is refused in the engine's words, and changes nothing")
{
    Instance b;
    b.block();
    const auto before = b.read ("state");
    auto& m = b.model();
    const auto garbage = m.paste ("https://example.com/not-a-slot");
    CHECK (garbage.kind == Transfer::Kind::refused);
    CHECK (garbage.reason == "The clipboard doesn't hold a Trance Gate slot.");
    CHECK (m.paste (std::string ("{\"sv\":7}") + '\0' + "trailing").kind == Transfer::Kind::refused);
    CHECK (m.paste (std::string (1 << 20, ' ')).kind == Transfer::Kind::refused);
    CHECK (m.paste ("").reason == "The clipboard is empty.");
    const auto file = m.importText ("{\"format\": \"ni-trance-gate-slot\", \"version\": 7}");
    CHECK (file.kind == Transfer::Kind::refused);
    CHECK (file.reason.rfind ("The file is version 7", 0) == 0);
    b.block();
    CHECK_FALSE (b.follow());
    CHECK (b.read ("state") == before);
}

TEST_CASE ("shuffle deals the arriving steps a fresh order, the pattern left as it is")
{
    Instance a;
    a.block();
    auto& m = a.model();
    const auto before = m.pattern();
    std::vector<int> orders;
    bool moved = false;
    for (int round = 0; round < 5 && ! moved; ++round)
    {
        m.shuffleOrder();
        a.block();
        moved = m.pattern().orders != before.orders;
    }
    CHECK (moved);
    CHECK (m.pattern().steps == before.steps);
    /* Still a permutation of the hits: 1..n, each once. */
    std::vector<int> ranks;
    for (int i = 0; i < m.pattern().length; ++i)
        if (m.pattern().steps[(std::size_t) i] != StepMode::off)
            ranks.push_back (m.pattern().orders[(std::size_t) i]);
    std::sort (ranks.begin(), ranks.end());
    for (std::size_t k = 0; k < ranks.size(); ++k)
        CHECK (ranks[k] == (int) k + 1);
}

/* --------------------------------------------------------------- editor -- */

TEST_CASE ("the editor on the real model: its window grows a row with Length, and its playhead moves")
{
    Instance a;
    a.block();
    Window window (a.model());
    auto& e = window.editor();
    double now = 0.0;
    e.tick (now);
    CHECK (window.getWidth() == TranceGateEditor::designWidth);
    const int sixteen = window.getHeight();
    CHECK (sixteen == TranceGateEditor::designHeightFor (16));
    CHECK (e.ring().getCount() == 16);

    /* The host's Length: the engine's pattern, the ring, a second row. */
    a.automate (kLength, 32.0);
    e.tick (now += 16.0);
    CHECK (e.ring().getCount() == 32);
    CHECK (window.getHeight() == TranceGateEditor::designHeightFor (32));
    CHECK (window.getHeight() > sixteen);

    /* The transport runs: the ring's playhead moves, and the ring is drawn
     * again with it. */
    a.p.editorOpened();
    a.play (4);
    e.tick (now += 16.0);
    const int first = e.ring().getPlayhead();
    const auto before = e.ring().createComponentSnapshot (e.ring().getLocalBounds());
    a.play (30);
    e.tick (now += 16.0);
    CHECK (first >= 0);
    CHECK (e.ring().getPlayhead() != first);
    const auto after = e.ring().createComponentSnapshot (e.ring().getLocalBounds());
    bool differs = false;
    for (int y = 0; y < before.getHeight() && ! differs; ++y)
        for (int x = 0; x < before.getWidth() && ! differs; ++x)
            differs = before.getPixelAt (x, y) != after.getPixelAt (x, y);
    CHECK (differs);
    a.p.editorClosed();
}

TEST_CASE ("the editor stays whole while a set loads on another thread")
{
    Instance a;
    a.block();
    Window window (a.model());
    auto& e = window.editor();
    e.tick (0.0);
    const auto slots = fixture ("slots");
    std::atomic<bool> done { false };
    std::thread host ([&] {
        a.p.setStateInformation (slots.data(), (int) slots.size());
        done = true;
    });
    host.join();
    REQUIRE (done.load());
    /* The parameters' news reaches the controls on the message thread. */
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    a.block();
    e.tick (16.0);
    CHECK (e.ring().getCount() == 32);
    CHECK (e.knob (kWidth).binding().value() == doctest::Approx (a.p.parameter (kWidth).getValue()));
    CHECK (a.value (kSlot) == 2.0);
}

TEST_CASE ("the window verbs copy, paste, export and import through the real engine")
{
    Instance a;
    a.block();
    for (const auto& [i, v] : sound (0))
        a.automate (i, v);
    test::FakeClipboard clipboard;
    test::FakeFilePanels panels;
    TranceGateEditor e (a.model(), clipboard, panels, [] { return 0.0; });
    e.tick (0.0);

    e.verbs().button (Verbs::Verb::copy).onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    REQUIRE (clipboard.text.has_value());
    CHECK (*clipboard.text == a.model().exportText (false));

    a.automate (kSlot, 2.0);
    e.verbs().button (Verbs::Verb::paste).onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    a.block();
    CHECK (a.follow());
    checkSound (a, 0);

    e.verbs().button (Verbs::Verb::exportAll).onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    REQUIRE (panels.asked.has_value());
    const auto bankFile = test::testFolder().getChildFile ("verbs.nitgbank");
    panels.answer (bankFile);
    REQUIRE (panels.files.count (bankFile.getFullPathName()) == 1);
    CHECK (panels.files[bankFile.getFullPathName()] == a.model().exportText (true));
    CHECK (a.model().lastFolder() == test::testFolder());

    Instance b;
    b.block();
    TranceGateEditor f (b.model(), clipboard, panels, [] { return 0.0; });
    f.verbs().button (Verbs::Verb::import).onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    REQUIRE (panels.asked.has_value());
    panels.answer (bankFile);
    b.block();
    b.follow();
    CHECK (b.model().exportText (true) == a.model().exportText (true));
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    doctest::Context context (argc, argv);
    return context.run();
}
