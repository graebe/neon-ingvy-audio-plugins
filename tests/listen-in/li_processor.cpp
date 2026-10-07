// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In on the JUCE shell, its processor in the program, on the real
 * bus: what a host sees, what a set keeps, what the bus carries, and the
 * native editor on the real model.
 *
 *   the parameter  Bus and Bypass as the iPlug2 build reported them
 *                  (parameters.json): names, units, steps, defaults, texts
 *   the fixtures   every tests/fixtures/iplug2/NIListenIn state loads to its
 *                  bus, its name and its bypass, and saves back byte for byte
 *   the state      save, a fresh instance, load, save again: the same bytes;
 *                  a headerless chunk opens; what no build wrote changes
 *                  nothing; a name is kept as wire::parse_label keeps it
 *   the bus        nothing claimed before the host prepares; then live, with
 *                  its name and rate; a second instance on the same bus says
 *                  taken and publishes nothing; release before claim, so
 *                  3 -> 4 -> 3 stays live; a set loaded on another thread
 *                  claims afresh under its name
 *   the audio      through bit for bit and on the bus, a block longer than
 *                  the stage included; the peak kept, clamped and decaying;
 *                  bypassed, the host's way -- through, and nothing published
 *   the model      the processor's status, peak and name; an edit marks the
 *                  set unsaved, a load does not; the Ground only with a window
 *   the editor     on the real model: the bus, the name -- or the placeholder,
 *                  never "(null)" -- and the LED, a set loaded on another
 *                  thread while it is open, and a name typed into it
 *
 * The editor's own behaviour against a model that does as told is tests/ui's
 * (listen-in_editor.cpp, on fakes); this is what only the real one can show.
 * li_host.cpp holds the built VST3, as a DAW hosts it.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "EngineModel.h"
#include "Info.h"
#include "ListenIn.h"
#include "ListenInEditor.h"
#include "PluginEditor.h"
#include "Wire.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace ni::li;
using Bytes = std::vector<std::uint8_t>;

namespace
{
const juce::File fixtures = juce::File (NI_FIXTURES).getChildFile ("NIListenIn");

Bytes fixture (const juce::String& scenario)
{
    juce::MemoryBlock b;
    REQUIRE (fixtures.getChildFile (scenario + ".component.bin").loadFileAsData (b));
    const auto* p = static_cast<const std::uint8_t*> (b.getData());
    return Bytes (p, p + b.getSize());
}

void putF64 (Bytes& out, double v)
{
    const auto* b = reinterpret_cast<const std::uint8_t*> (&v);
    out.insert (out.end(), b, b + 8);
}

void putI32 (Bytes& out, std::int32_t v)
{
    const auto* b = reinterpret_cast<const std::uint8_t*> (&v);
    out.insert (out.end(), b, b + 4);
}

/* What a receiver sees of a bus without opening it. */
struct Seen
{
    bool exists = false;
    bool live = false;
    std::uint32_t rate = 0;
    std::string label;
};

Seen probe (int bus)
{
    Seen s;
    std::int32_t live = 0;
    char label[ABUS_LABEL_CAP] {};
    s.exists = abus_probe ((std::uint32_t) bus, &live, &s.rate, label, ABUS_LABEL_CAP) == 1;
    s.live = live != 0;
    s.label = label;
    return s;
}

/* A receiver on one bus, as a Spectrogram opens it. */
struct Reader
{
    abus_reader_t* r = nullptr;

    explicit Reader (int bus) { REQUIRE (abus_reader_open ((std::uint32_t) bus, &r) == ABUS_OK); }
    ~Reader() { abus_reader_close (r); }

    /* Everything waiting, interleaved. */
    std::vector<float> drain (std::uint64_t* dropped = nullptr)
    {
        std::vector<float> out, chunk (8192 * 2);
        for (;;)
        {
            std::uint64_t lost = 0;
            const auto n = abus_reader_read (r, chunk.data(), 8192, &lost, nullptr);
            if (dropped != nullptr)
                *dropped += lost;
            if (n == 0)
                return out;
            out.insert (out.end(), chunk.begin(), chunk.begin() + (std::ptrdiff_t) n * 2);
        }
    }
};

/* What the host is told: every set marked unsaved. */
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

/* A transport playing at 120 BPM in 4/4 from bar 1. */
struct Playhead final : juce::AudioPlayHead
{
    juce::int64 sample = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true);
        p.setBpm (120.0);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setTimeInSamples (sample);
        p.setPpqPosition ((double) sample / 48000.0 * 2.0);
        return p;
    }
};

/* One instance, as a host runs it. */
struct Instance
{
    Processor p;
    HostEar ear;

    explicit Instance (bool prepared = true)
    {
        if (prepared)
            p.prepareToPlay (48000.0, 512);
        p.addListener (&ear);
    }
    ~Instance() { p.removeListener (&ear); }

    EngineModel& model() { return p.model(); }
    int bus() const { return (int) p.busParameter().plain(); }
    bool bypassed() const { return p.getBypassParameter()->getValue() >= 0.5f; }
    void setBypass (bool on) { p.getBypassParameter()->setValueNotifyingHost (on ? 1.0f : 0.0f); }

    /* The host moves Bus, and the message thread's next tick follows it. */
    void moveTo (int bus)
    {
        p.busParameter().setPlainNotifyingHost (bus);
        p.serviceBus();
    }

    /* One block of `in`, through processBlock as the VST3 wrapper calls it:
     * what came out. */
    juce::AudioBuffer<float> run (const juce::AudioBuffer<float>& in)
    {
        juce::AudioBuffer<float> buffer (in);
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);
        return buffer;
    }

    Bytes save()
    {
        juce::MemoryBlock m;
        p.getStateInformation (m);
        const auto* b = static_cast<const std::uint8_t*> (m.getData());
        return Bytes (b, b + m.getSize());
    }

    void load (const Bytes& b) { p.setStateInformation (b.data(), (int) b.size()); }
};

/* Noise in [-1, 1), stereo, `frames` long. */
juce::AudioBuffer<float> noise (int frames, unsigned seed, int channels = 2)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    juce::AudioBuffer<float> b (channels, frames);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < frames; ++i)
            b.setSample (ch, i, d (rng));
    return b;
}

/* A buffer's channels interleaved, as the bus carries them; a lone channel
 * on both sides. */
std::vector<float> interleaved (const juce::AudioBuffer<float>& b)
{
    std::vector<float> out;
    const int right = b.getNumChannels() > 1 ? 1 : 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        out.push_back (b.getSample (0, i));
        out.push_back (b.getSample (right, i));
    }
    return out;
}

bool same (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
            return false;
    return true;
}

ListenInEditor& designOf (juce::AudioProcessorEditor& e)
{
    return dynamic_cast<ListenInEditor&> (dynamic_cast<ni::PluginEditor&> (e).getDesign());
}
} // namespace

/* ------------------------------------------------------------ parameter -- */

TEST_CASE ("Bus and Bypass, as the iPlug2 build reported them")
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
    CHECK (count == 2);
    /* With legacy IDs the index is the VST3 ID: Bypass is the last, 1. */
    CHECK (a.p.getParameters().size() == 2);
    CHECK (a.p.getBypassParameter() == a.p.getParameters()[kNumParams]);
    CHECK (a.p.getBypassParameter()->isBoolean());
    CHECK (&a.p.busParameter() == a.p.getParameters()[kBus]);
    CHECK (numBuses == ABUS_MAX_SLOT);
}

TEST_CASE ("every bus reads back from its own text, and nothing lands off the sixteen")
{
    Instance a;
    auto& bus = a.p.busParameter();
    for (int b = 1; b <= numBuses; ++b)
    {
        CAPTURE (b);
        bus.setPlain (b);
        const auto shown = bus.getText (bus.getValue(), 128);
        CHECK (shown == juce::String (b));
        CHECK (bus.convertFrom0to1 (bus.getValueForText (shown)) == doctest::Approx (b));
    }
    bus.setPlain (0.0);
    CHECK (a.bus() == 1);
    bus.setPlain (40.0);
    CHECK (a.bus() == numBuses);
    CHECK (wire::clamp_slot (0) == 1);
    CHECK (wire::clamp_slot (-3) == 1);
    CHECK (wire::clamp_slot (17) == numBuses);
    CHECK (wire::clamp_slot (9) == 9);
}

/* ------------------------------------------------------------- the state -- */

TEST_CASE ("every iPlug2 fixture loads to its bus, its name and its bypass, and saves back byte for byte")
{
    for (const char* scenario : { "default", "labelled" })
    {
        CAPTURE (scenario);
        const auto doc = juce::JSON::parse (fixtures.getChildFile (juce::String (scenario) + ".json"));
        const auto& decoded = doc["decoded"];
        Instance a;
        a.moveTo (7);
        a.p.editLabel ("something else");
        a.setBypass (true);

        const auto chunk = fixture (scenario);
        a.load (chunk);
        CHECK (a.bus() == (int) (*decoded["params"].getArray())[0]);
        CHECK (juce::String::fromUTF8 (a.p.label().c_str()) == (*decoded["strings"].getArray())[0].toString());
        CHECK (a.bypassed() == ((int) decoded["bypass"] != 0));
        for (const auto& v : *doc["values"].getArray())
        {
            const int id = v["id"];
            auto* param = id == 65536 ? a.p.getBypassParameter() : a.p.getParameters()[id];
            CHECK (param->getText (param->getValue(), 128) == v["display"].toString());
        }
        CHECK (a.save() == chunk);
    }
}

TEST_CASE ("save, a fresh instance, load, save again: the same bytes")
{
    std::mt19937 rng (11);
    const char* names[] { "", "Kick", "Bässe & Kick", "drums \xe2\x80\x94 room", "0123456789012345678901234567890" };
    for (int round = 0; round < 10; ++round)
    {
        CAPTURE (round);
        Instance a;
        a.moveTo (1 + (int) (rng() % numBuses));
        a.p.editLabel (names[rng() % std::size (names)]);
        a.setBypass (rng() % 2 == 1);
        const auto saved = a.save();

        Instance b;
        b.load (saved);
        CHECK (b.bus() == a.bus());
        CHECK (b.p.label() == a.p.label());
        CHECK (b.bypassed() == a.bypassed());
        CHECK (b.save() == saved);
    }
}

TEST_CASE ("a headerless chunk from the builds before the header opens")
{
    /* FORMAT.md: double[1], the name as a length and its bytes, the bypass. */
    Bytes legacy;
    putF64 (legacy, 5.0);
    const std::string name = "Pad bus";
    putI32 (legacy, (std::int32_t) name.size());
    legacy.insert (legacy.end(), name.begin(), name.end());
    putI32 (legacy, 1);

    Instance a;
    a.load (legacy);
    CHECK (a.bus() == 5);
    CHECK (a.p.label() == name);
    CHECK (a.bypassed());
    /* Written back in today's form, which every build reads. */
    const auto saved = a.save();
    REQUIRE (saved.size() > 4);
    CHECK (std::memcmp (saved.data(), "NIst", 4) == 0);
}

TEST_CASE ("what no build wrote is refused, and changes nothing")
{
    Instance a;
    a.moveTo (6);
    a.p.editLabel ("kept");
    const auto before = a.save();
    std::mt19937 rng (7);
    for (int round = 0; round < 3; ++round)
    {
        Bytes noise (4096);
        for (auto& x : noise)
            x = (std::uint8_t) rng();
        a.load (noise);
        CHECK (a.save() == before);
    }
    auto cut = fixture ("labelled");
    cut.resize (cut.size() - 9);
    a.load (cut);
    CHECK (a.save() == before);
    a.load ({});
    CHECK (a.save() == before);
    CHECK (a.bus() == 6);
    CHECK (a.p.label() == "kept");
}

TEST_CASE ("a name is kept as the plugin keeps it: no colon, no control character, cut on a character")
{
    Instance a;
    a.p.editLabel ("kick:\x01" "bus\x7f");
    CHECK (a.p.label() == "kickbus");

    /* 30 ASCII bytes and an umlaut: 32 bytes, so the umlaut goes whole. */
    const std::string thirty (30, 'x');
    a.p.editLabel (thirty + "\xc3\xa4");
    CHECK (a.p.label() == thirty);
    /* 29 and an umlaut: 31, which fits. */
    a.p.editLabel (thirty.substr (1) + "\xc3\xa4");
    CHECK (a.p.label() == thirty.substr (1) + "\xc3\xa4");

    /* A set saved by another hand opens with the name cleaned. */
    Bytes legacy;
    putF64 (legacy, 2.0);
    const std::string dirty = "a:b\tc";
    putI32 (legacy, (std::int32_t) dirty.size());
    legacy.insert (legacy.end(), dirty.begin(), dirty.end());
    putI32 (legacy, 0);
    a.load (legacy);
    CHECK (a.p.label() == "abc");

    char out[ABUS_LABEL_CAP];
    CHECK (wire::parse_label (nullptr, out, (int) sizeof out) == 0);
    CHECK (out[0] == '\0');
    CHECK (wire::parse_label ("x", nullptr, 8) == 0);
    /* A sequence the text ends inside is dropped, wherever the end is. */
    CHECK (wire::parse_label ("ab\xc3", out, (int) sizeof out) == 2);
    CHECK (std::string (out) == "ab");
}

/* --------------------------------------------------------------- the bus -- */

TEST_CASE ("nothing is claimed before the host prepares; then the bus is live, with its name and rate")
{
    Instance a (false);
    a.p.busParameter().setPlainNotifyingHost (2.0);
    a.p.editLabel ("Bass");
    a.p.serviceBus();
    CHECK (a.p.status() == Status::idle);
    CHECK_FALSE (probe (2).live);

    a.p.prepareToPlay (48000.0, 512);
    a.p.serviceBus();
    CHECK (a.p.status() == Status::live);
    a.run (noise (64, 1));
    const auto seen = probe (2);
    CHECK (seen.live);
    CHECK (seen.rate == 48000);
    CHECK (seen.label == "Bass");

    /* A new name reaches the bus at once. */
    a.p.editLabel ("Bass DI");
    CHECK (probe (2).label == "Bass DI");

    /* A host that restarts at another rate retunes the bus. */
    a.p.prepareToPlay (44100.0, 512);
    a.p.serviceBus();
    a.run (noise (64, 2));
    CHECK (a.p.status() == Status::live);
    CHECK (probe (2).rate == 44100);
}

TEST_CASE ("a second instance on a held bus says taken and publishes nothing, until it moves")
{
    auto first = std::make_unique<Instance>();
    first->moveTo (3);
    first->p.editLabel ("first");
    CHECK (first->p.status() == Status::live);

    Instance second;
    second.moveTo (3);
    CHECK (second.p.status() == Status::taken);
    second.p.editLabel ("second");
    Reader reader (3);
    second.run (noise (256, 3));
    CHECK (reader.drain().empty());
    CHECK (probe (3).label == "first");

    second.moveTo (4);
    CHECK (second.p.status() == Status::live);
    CHECK (probe (4).label == "second");

    /* The first goes away; the second does not take over by itself, but
     * moving back is enough. */
    first.reset();
    CHECK_FALSE (probe (3).live);
    second.p.serviceBus();
    CHECK (second.p.status() == Status::live);
    second.moveTo (3);
    CHECK (second.p.status() == Status::live);
    CHECK (probe (3).label == "second");
    CHECK_FALSE (probe (4).live);
}

TEST_CASE ("release first, then claim: 3 -> 4 -> 3 stays live")
{
    Instance a;
    for (int bus : { 3, 4, 3 })
    {
        CAPTURE (bus);
        a.moveTo (bus);
        CHECK (a.p.status() == Status::live);
        CHECK (probe (bus).live);
    }
    CHECK_FALSE (probe (4).live);
}

TEST_CASE ("a set loaded on the host's thread claims afresh, under its own name")
{
    Instance a;
    a.moveTo (8);
    a.p.editLabel ("before");
    const auto labelled = fixture ("labelled");
    std::thread host ([&] { a.load (labelled); });
    host.join();
    /* What a save writes is the load, before the message thread has seen it. */
    CHECK (a.save() == labelled);
    a.p.serviceBus();
    CHECK (a.p.status() == Status::live);
    CHECK_FALSE (probe (8).live);
    CHECK (probe (3).live);
    CHECK (probe (3).label == "Bässe & Kick");
}

TEST_CASE ("the timer serves the bus on the message thread by itself")
{
    Instance a;
    a.p.busParameter().setPlainNotifyingHost (12.0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    CHECK (a.p.status() == Status::live);
    CHECK (probe (12).live);
}

/* ------------------------------------------------------------- the audio -- */

TEST_CASE ("the audio passes through bit for bit and is published on the bus as it came")
{
    Instance a;
    a.moveTo (5);
    Reader reader (5);
    std::uint64_t dropped = 0;

    const auto in = noise (512, 4);
    CHECK (same (a.run (in), in));
    CHECK (reader.drain (&dropped) == interleaved (in));

    /* Longer than the stage: pushed in chunks, nothing lost or reordered. */
    const auto longer = noise (Processor::stageFrames + 904, 5);
    CHECK (same (a.run (longer), longer));
    CHECK (reader.drain (&dropped) == interleaved (longer));

    /* A lone channel is published on both sides. */
    const auto mono = noise (300, 6, 1);
    CHECK (same (a.run (mono), mono));
    CHECK (reader.drain (&dropped) == interleaved (mono));
    CHECK (dropped == 0);
}

TEST_CASE ("bypassed, the host's way: the audio through, nothing published, and the meter falls")
{
    Instance a;
    a.moveTo (10);
    Reader reader (10);
    juce::AudioBuffer<float> loud (2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            loud.setSample (ch, i, 0.5f);
    a.run (loud);
    reader.drain();
    CHECK (a.p.peak() == doctest::Approx (0.5f));

    /* JUCE's VST3 wrapper hands a processor with its own bypass parameter
     * every block through processBlock, and the processor reads it. */
    a.setBypass (true);
    const auto in = noise (512, 7);
    CHECK (same (a.run (in), in));
    CHECK (reader.drain().empty());
    CHECK (a.p.peak() < 0.5f);

    a.setBypass (false);
    a.run (in);
    CHECK (reader.drain() == interleaved (in));

    /* A host that bypasses the plugin itself. */
    juce::AudioBuffer<float> through (in);
    juce::MidiBuffer midi;
    a.p.processBlockBypassed (through, midi);
    CHECK (same (through, in));
    CHECK (reader.drain().empty());
}

TEST_CASE ("the peak is the block's loudest sample, clamped to full scale, and decays rather than resets")
{
    Instance a;
    juce::AudioBuffer<float> b (2, 256);
    b.clear();
    b.setSample (1, 100, -0.75f);
    a.run (b);
    CHECK (a.p.peak() == doctest::Approx (0.75f));
    CHECK (a.model().peak() == a.p.peak());

    b.clear();
    a.run (b);
    CHECK (a.p.peak() == doctest::Approx (0.75f * 0.85f));
    for (int i = 0; i < 200; ++i)
        a.run (b);
    CHECK (a.p.peak() < 1.0e-6f);

    b.setSample (0, 0, 3.0f);
    a.run (b);
    CHECK (a.p.peak() == 1.0f);
}

/* ------------------------------------------------------------- the model -- */

TEST_CASE ("the model is the processor's: the bus parameter, the status, the peak, the name")
{
    Instance a;
    auto& m = a.model();
    CHECK (m.numParameters() == 1);
    CHECK (&m.parameter (param::bus) == &a.p.busParameter());
    CHECK (m.status() == Status::idle);
    a.moveTo (13);
    CHECK (m.status() == Status::live);
    CHECK (m.label().isEmpty());

    m.setLabel (juce::String::fromUTF8 ("Fl\xc3\xa4" "chen: pad"));
    /* Kept from the moment it returns. */
    CHECK (m.label() == juce::String::fromUTF8 ("Fl\xc3\xa4" "chen pad"));
    CHECK (probe (13).label == "Fl\xc3\xa4" "chen pad");
}

TEST_CASE ("a name typed marks the set unsaved; the same name, a load and the host's bus do not")
{
    Instance a;
    a.load (fixture ("labelled"));
    a.moveTo (9);
    a.p.serviceBus();
    CHECK (a.ear.dirty == 0);

    a.model().setLabel ("new name");
    CHECK (a.ear.dirty == 1);
    a.model().setLabel ("new name");
    CHECK (a.ear.dirty == 1);
    /* What is typed is not what is kept: the colon goes, so the same name. */
    a.model().setLabel ("new: name");
    CHECK (a.ear.dirty == 1);
    a.model().setLabel ("newer name");
    CHECK (a.ear.dirty == 2);
}

TEST_CASE ("the Ground rings with the host's beat, only while a window is open")
{
    Instance a;
    Playhead head;
    a.p.setPlayHead (&head);
    juce::AudioBuffer<float> b (2, 512);
    const auto play = [&] (int blocks)
    {
        for (int i = 0; i < blocks; ++i)
        {
            b.clear();
            a.run (b);
            head.sample += 512;
        }
    };
    float rings[16];
    play (200);
    a.p.editorOpened();
    CHECK (a.model().takeRings (rings, 16) == 0);
    play (94);
    const int n = a.model().takeRings (rings, 16);
    CHECK (n >= 1);
    CHECK (n <= 3);

    /* And bypassed too: the Ground keeps the host's time. */
    a.setBypass (true);
    play (94);
    CHECK (a.model().takeRings (rings, 16) >= 1);
    a.p.editorClosed();
    a.p.setPlayHead (nullptr);
}

/* ------------------------------------------------------------ the editor -- */

TEST_CASE ("the editor on the real model: its bus, its name or the placeholder, and the LED")
{
    Instance a;
    std::unique_ptr<juce::AudioProcessorEditor> window (a.p.createEditor());
    REQUIRE (window != nullptr);
    auto& e = designOf (*window);
    CHECK (window->getWidth() == ListenInEditor::designWidth);
    CHECK (window->getHeight() == ListenInEditor::designHeight);

    e.refresh();
    CHECK (e.busSelect().getIndex() == 0);
    CHECK (e.busSelect().getOptions().size() == numBuses);
    CHECK (e.busSelect().getOptions()[15] == "16");
    CHECK (e.nameField().getValue().isEmpty());
    CHECK (e.statusLed().getLabel() == "idle");
    for (const auto& line : ni::ui::collectInfo (e))
        CHECK_FALSE (line.contains ("null"));

    a.moveTo (11);
    e.refresh();
    CHECK (e.busSelect().getIndex() == 10);
    CHECK (e.statusLed().getLabel() == "listening");
    CHECK (e.statusLed().getStatus() == ni::ui::Led::Status::on);
    CHECK (ni::ui::infoOf (e.statusLed()).contains ("listening on bus 11, unnamed."));

    /* Another instance holds 12: the LED says so. */
    Instance holder;
    holder.moveTo (12);
    a.moveTo (12);
    e.refresh();
    CHECK (e.statusLed().getLabel() == "slot taken");
    CHECK (e.statusLed().getStatus() == ni::ui::Led::Status::warn);
}

TEST_CASE ("the editor shows a set loaded on another thread while it is open")
{
    Instance a;
    a.moveTo (2);
    std::unique_ptr<juce::AudioProcessorEditor> window (a.p.createEditor());
    auto& e = designOf (*window);
    e.refresh();

    const auto labelled = fixture ("labelled");
    std::thread host ([&] { a.load (labelled); });
    host.join();
    /* The parameter's news reaches the select on the message thread, and
     * the timer claims the loaded bus. */
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    e.refresh();
    CHECK (e.busSelect().getIndex() == 2);
    CHECK (e.nameField().getValue() == juce::String::fromUTF8 ("B\xc3\xa4sse & Kick"));
    CHECK (e.statusLed().getLabel() == "listening");
    CHECK (probe (3).label == "Bässe & Kick");
}

TEST_CASE ("a name typed into the editor is kept, published, and shown as kept")
{
    Instance a;
    a.moveTo (14);
    std::unique_ptr<juce::AudioProcessorEditor> window (a.p.createEditor());
    auto& e = designOf (*window);
    REQUIRE (e.nameField().onCommit != nullptr);
    e.nameField().onCommit ("kick: room");
    CHECK (e.nameField().getValue() == "kick room");
    CHECK (a.p.label() == "kick room");
    CHECK (probe (14).label == "kick room");
    CHECK (a.ear.dirty == 1);
    window.reset();
}

int main (int argc, char** argv)
{
    /* A bus namespace of this process's own: never a Live session's buses,
     * nor another test's. Set before the first bus call. */
    const auto ns = "li_processor." + std::to_string ((long) getpid());
    setenv ("NIA_BUS_NS", ns.c_str(), 1);

    const juce::ScopedJuceInitialiser_GUI gui;
    doctest::Context context (argc, argv);
    return context.run();
}
