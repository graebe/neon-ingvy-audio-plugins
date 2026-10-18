// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Vocalize's processor on the real engine: what only the shell around it
 * can get wrong.
 *
 *   the parameters  declared from the engine's table: its keys as IDs, its
 *                   names, ranges, defaults and words; typing read by the
 *                   engine, a vowel's letter included, junk changing
 *                   nothing; a value on the resolution coming back exactly.
 *                   Mode a choice of the engine's labels no host may automate
 *   the latency     Mode's, at the rate prepared: none for Time, one frame
 *                   for Spectral -- told to the host on the message thread
 *                   when Mode moves, and at once when the plugin is prepared;
 *                   and exactly the delay the dry signal is heard with
 *   the buses       mono or stereo, the output as the input
 *   the sound       Vocal 0 and Excite 0 are the input bit for bit, stereo
 *                   and mono; a reactivation lets go of what was held, in
 *                   either engine; the host's bypass passes the audio, as
 *                   late as the latency reported
 *   the state       saved and reopened, every value exactly
 *   the voice       captured from a recording on a thread of its own: the
 *                   vowels sung measured, the rest filled in; sung by the
 *                   engine as far as Voice says; kept in the set as numbers
 *                   and reopened, a set without one -- or with one that is not
 *                   one -- forgetting it; forgotten by hand; a recording that
 *                   cannot be read, has no singing or no vowels in it,
 *                   saying which; each vowel's confidence and whether the
 *                   order told them apart, kept; Forget and one Restore; a
 *                   second capture ignored while one runs; a set read during
 *                   a capture winning; the processor closed under a capture,
 *                   at once
 *   the ceiling     nothing past full scale with it on
 *   the bypass      the engine listens through it and resumes from what it
 *                   heard, in time
 *   the model       the formants the engine's, at Size and Resonance now;
 *                   the picture after audio, saying which engine drew it,
 *                   its envelopes only while a window watches;
 *                   the knobs that run both ways
 *   the window      opens at the design's size; on the real model its
 *                   envelope names the engine and its latency
 *
 * What is the editor's own behaviour against a model that does as told is
 * tests/ui's (vocalize_editor.cpp, on fakes); vz_host.cpp holds the built
 * VST3, as a DAW hosts it.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "Editor.h"
#include "Vocalize.h"
#include "vz_voices.h"

#include "state_fixture.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

using ni::vz::Processor;
namespace param = ni::vz::param;

namespace
{
constexpr int block = 512;
constexpr double rate = 48000.0;

/* One frame of the Spectral engine at 44.1 and 48 kHz, and at 88.2 and
 * 96 kHz (vz_latency). */
constexpr int frame = 1024;
constexpr int doubleFrame = 2048;

struct Instance
{
    Processor p;
    juce::AudioBuffer<float> buffer { 2, block };

    explicit Instance (int channels = 2, double sampleRate = rate)
    {
        if (channels == 1)
        {
            auto layout = p.getBusesLayout();
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
            layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
            REQUIRE (p.setBusesLayout (layout));
            buffer.setSize (1, block);
        }
        p.prepareToPlay (sampleRate, block);
    }

    void set (int index, double value)
    {
        auto& prm = p.parameter (index);
        prm.setValueNotifyingHost (prm.convertTo0to1 ((float) value));
    }

    /* `blocks` blocks of `fill` (channel, sample) -> value, as the host runs
     * them. */
    void run (const std::function<float (int, int)>& fill, int blocks = 1, bool bypassed = false)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                for (int i = 0; i < block; ++i)
                    buffer.setSample (c, i, fill (c, i));
            juce::MidiBuffer midi;
            auto& host = static_cast<juce::AudioProcessor&> (p);
            if (bypassed)
                host.processBlockBypassed (buffer, midi);
            else
                host.processBlock (buffer, midi);
        }
    }

    void run (float level, int blocks = 1, bool bypassed = false)
    {
        run ([level] (int, int) { return level; }, blocks, bypassed);
    }

    /* A second of silence: every glide has landed, a Mode switch's dip is
     * over, and nothing is left in the frames. */
    void settle() { run (0.0f, (int) rate / block + 1); }
};

/* The message loop runs until `done`, or for a second at most: the latency
 * follows Mode there. */
bool pumpUntil (const std::function<bool()>& done)
{
    const auto until = juce::Time::getMillisecondCounter() + 1000;
    while (! done() && juce::Time::getMillisecondCounter() < until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return done();
}

/* A sawtooth with a breath of noise: something for a vowel to be put on, and
 * different on the two channels. The same numbers for the same sample. */
float source (juce::int64 n, int channel)
{
    const double phase = std::fmod (110.0 * (double) n / rate, 1.0);
    juce::Random noise (n * 2 + channel);
    return (float) (0.35 * (2.0 * phase - 1.0) + 0.05 * (noise.nextDouble() - 0.5));
}

/* What the host is told: every latency change it was notified of, and every
 * time the set was marked unsaved. */
struct HostListener final : juce::AudioProcessorListener
{
    int latencyChanges = 0;
    /* State the host cannot see changed: the set marked unsaved. */
    int unsaved = 0;

    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override
    {
        latencyChanges += d.latencyChanged ? 1 : 0;
        unsaved += d.nonParameterStateChanged ? 1 : 0;
    }
};

juce::String engineText (int (*read) (int32_t, char*, int), int index)
{
    char buf[64] {};
    return read (index, buf, (int) sizeof buf) >= 0 ? juce::String::fromUTF8 (buf) : juce::String();
}
} // namespace

/* ------------------------------------------------------- the parameters -- */

TEST_CASE ("the parameters are the engine's table, in its order and words")
{
    Instance a;
    REQUIRE (vz_param_count() == Processor::paramCount);
    CHECK (a.p.getParameters().size() == Processor::paramCount);
    CHECK (a.p.getBypassParameter() == nullptr);

    std::set<juce::String> ids;
    for (int i = 0; i < Processor::paramCount; ++i)
    {
        auto& prm = a.p.parameter (i);
        CAPTURE (i);
        NiParamInfo info {};
        REQUIRE (vz_param_info (i, &info));
        CHECK (prm.getParameterIndex() == i);
        CHECK (prm.isAutomatable() == info.automatable);
        CHECK (prm.getParameterID() == engineText (vz_param_key, i));
        CHECK (prm.getName (32) == engineText (vz_param_name, i));
        CHECK (prm.convertFrom0to1 (0.0f) == (float) info.min);
        CHECK (prm.convertFrom0to1 (1.0f) == (float) info.max);
        CHECK (a.p.plain (i) == info.default_value);
        ids.insert (prm.getParameterID());
    }
    CHECK (ids == std::set<juce::String> { "mode", "vocal", "vowel", "size", "resonance", "excite", "mix", "output", "voice", "ceiling" });

    /* The words a host lists at the defaults. */
    const auto text = [&a] (int i) { return a.p.parameter (i).getCurrentValueAsText(); };
    CHECK (text (param::mode) == "Time");
    CHECK (text (param::vocal) == "80 %");
    CHECK (text (param::vowel) == "A");
    CHECK (text (param::size) == "0 %");
    CHECK (text (param::resonance) == "50 %");
    CHECK (text (param::excite) == "0 %");
    CHECK (text (param::mix) == "100 %");
    CHECK (text (param::output) == "0.0 dB");
    CHECK (text (param::voice) == "100 %");
    CHECK (text (param::ceiling) == "On");
}

TEST_CASE ("Mode is a choice of the engine's two labels, which no host may automate; Ceiling one it may")
{
    Instance a;
    auto* mode = dynamic_cast<juce::AudioParameterChoice*> (&a.p.parameter (param::mode));
    REQUIRE (mode != nullptr);
    CHECK (mode->choices == juce::StringArray { "Time", "Spectral" });
    CHECK_FALSE (mode->isAutomatable());
    auto* ceiling = dynamic_cast<juce::AudioParameterChoice*> (&a.p.parameter (param::ceiling));
    REQUIRE (ceiling != nullptr);
    CHECK (ceiling->choices == juce::StringArray { "Off", "On" });
    CHECK (ceiling->getIndex() == 1);
    CHECK (ceiling->isAutomatable());
    for (int i = 0; i < Processor::paramCount; ++i)
        if (i != param::mode)
            CHECK_MESSAGE (a.p.parameter (i).isAutomatable(), a.p.parameter (i).getParameterID());

    a.set (param::mode, 1.0);
    CHECK (a.p.plain (param::mode) == 1.0);
    CHECK (mode->getCurrentValueAsText() == "Spectral");
}

TEST_CASE ("a value on the table's resolution comes back exactly, in the engine's words")
{
    Instance a;
    a.set (param::vowel, 1.3);
    CHECK (a.p.plain (param::vowel) == 1.3);
    CHECK (a.p.parameter (param::vowel).getCurrentValueAsText() == "E>I 30 %");
    a.set (param::vowel, 1.7);
    CHECK (a.p.plain (param::vowel) == 1.7);
    CHECK (a.p.parameter (param::vowel).getCurrentValueAsText() == "E>I 70 %");
    a.set (param::vowel, 3.0);
    CHECK (a.p.parameter (param::vowel).getCurrentValueAsText() == "O");

    a.set (param::size, 30.0);
    CHECK (a.p.plain (param::size) == 30.0);
    CHECK (a.p.parameter (param::size).getCurrentValueAsText() == "+30 %");
    a.set (param::size, -45.0);
    CHECK (a.p.parameter (param::size).getCurrentValueAsText() == "-45 %");
    a.set (param::output, -3.0);
    CHECK (a.p.plain (param::output) == -3.0);
    CHECK (a.p.parameter (param::output).getCurrentValueAsText() == "-3.0 dB");
    a.set (param::output, 6.5);
    CHECK (a.p.parameter (param::output).getCurrentValueAsText() == "+6.5 dB");
    a.set (param::resonance, 65.0);
    CHECK (a.p.plain (param::resonance) == 65.0);
}

TEST_CASE ("typing is the engine's to read -- a vowel's letter too -- and junk changes nothing")
{
    Instance a;
    auto& v = a.p.parameter (param::vowel);
    const auto type = [] (juce::RangedAudioParameter& p, const char* text)
    { p.setValueNotifyingHost (p.getValueForText (text)); };

    type (v, "i");
    CHECK (a.p.plain (param::vowel) == 2.0);
    type (v, "E 30");
    CHECK (a.p.plain (param::vowel) == 1.3);
    type (v, "U");
    CHECK (a.p.plain (param::vowel) == 4.0);
    type (v, "E>I 30 %");
    CHECK (a.p.plain (param::vowel) == 1.3);
    type (v, "loud");
    CHECK (a.p.plain (param::vowel) == 1.3);
    type (v, "2.25");
    CHECK (a.p.plain (param::vowel) == 2.25);
    CHECK (v.getCurrentValueAsText() == "I>O 25 %");

    auto& size = a.p.parameter (param::size);
    type (size, "-45");
    CHECK (a.p.plain (param::size) == -45.0);
    type (size, "bigger");
    CHECK (a.p.plain (param::size) == -45.0);
    type (size, "500");
    CHECK (a.p.plain (param::size) == 100.0);

    type (a.p.parameter (param::output), "-6 dB");
    CHECK (a.p.plain (param::output) == -6.0);
}

/* ---------------------------------------------------------- the latency -- */

TEST_CASE ("the latency is Mode's, and the host is told on the message thread")
{
    Instance a;
    HostListener host;
    a.p.addListener (&host);
    CHECK (a.p.getLatencySamples() == 0);
    CHECK (a.p.latencyForMode() == 0);

    a.set (param::mode, 1.0);
    /* Asked for at once; told to the host once the message thread runs. */
    CHECK (a.p.latencyForMode() == frame);
    CHECK (pumpUntil ([&a] { return a.p.getLatencySamples() == frame; }));
    CHECK (host.latencyChanges >= 1);

    const int told = host.latencyChanges;
    a.set (param::mode, 0.0);
    CHECK (pumpUntil ([&a] { return a.p.getLatencySamples() == 0; }));
    CHECK (host.latencyChanges > told);

    /* A change to any other parameter leaves the latency where it is. */
    a.set (param::vocal, 20.0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK (a.p.getLatencySamples() == 0);
    a.p.removeListener (&host);
}

TEST_CASE ("a frame is twice as long at 88.2 and 96 kHz, and prepareToPlay says so at once")
{
    Instance a (2, 96000.0);
    a.set (param::mode, 1.0);
    CHECK (pumpUntil ([&a] { return a.p.getLatencySamples() == doubleFrame; }));

    /* A rate change is told in prepareToPlay itself, with no message loop. */
    a.p.prepareToPlay (88200.0, block);
    CHECK (a.p.getLatencySamples() == doubleFrame);
    a.p.prepareToPlay (44100.0, block);
    CHECK (a.p.getLatencySamples() == frame);
    a.p.prepareToPlay (rate, block);
    CHECK (a.p.getLatencySamples() == frame);
}

TEST_CASE ("a set saved in Spectral reports its latency as soon as it is prepared")
{
    Instance a;
    a.set (param::mode, 1.0);
    juce::MemoryBlock state;
    a.p.getStateInformation (state);

    /* As a host opens a set: a fresh instance, the state, then activation --
     * no message loop in between. */
    Processor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    b.prepareToPlay (rate, block);
    CHECK (b.getLatencySamples() == frame);
}

TEST_CASE ("the latency reported is exactly how late the dry signal is heard")
{
    for (const double mode : { 0.0, 1.0 })
    {
        CAPTURE (mode);
        Instance a;
        a.set (param::mode, mode);
        a.set (param::mix, 0.0);
        a.settle();
        REQUIRE (pumpUntil ([&a] { return a.p.getLatencySamples() == a.p.latencyForMode(); }));
        const int latency = a.p.getLatencySamples();

        /* One click, then silence: at Mix 0 it comes out once, whole, as
         * late as the host was told. Half scale, under the ceiling's knee,
         * which passes it bit for bit. */
        std::vector<float> out;
        for (int b = 0; b < 4; ++b)
        {
            a.run ([b] (int, int i) { return b == 0 && i == 0 ? 0.5f : 0.0f; });
            out.insert (out.end(), a.buffer.getReadPointer (0), a.buffer.getReadPointer (0) + block);
        }
        const auto peak = std::max_element (out.begin(), out.end(), [] (float x, float y) { return std::abs (x) < std::abs (y); });
        CHECK (peak - out.begin() == latency);
        CHECK (*peak == 0.5f);
        CHECK (std::count_if (out.begin(), out.end(), [] (float x) { return ! juce::exactlyEqual (x, 0.0f); }) == 1);
    }
}

/* ------------------------------------------------------------ the buses -- */

TEST_CASE ("mono and stereo, the output as the input, and nothing else")
{
    Processor p;
    auto layout = p.getBusesLayout();
    CHECK (p.checkBusesLayoutSupported (layout));
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (! p.checkBusesLayoutSupported (layout));
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (p.checkBusesLayoutSupported (layout));
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    CHECK (! p.checkBusesLayoutSupported (layout));
    CHECK (! p.acceptsMidi());
    CHECK (p.getTailLengthSeconds() == 0.0);
}

/* ------------------------------------------------------------ the sound -- */

TEST_CASE ("Vocal 0 and Excite 0 are the input, bit for bit, stereo and mono")
{
    for (const int channels : { 2, 1 })
    {
        CAPTURE (channels);
        Instance a (channels);
        a.set (param::vocal, 0.0);
        a.set (param::vowel, 2.6);
        a.set (param::resonance, 90.0);
        juce::int64 n = 0;
        const auto play = [&a, &n] (int blocks)
        {
            for (int b = 0; b < blocks; ++b, n += block)
                a.run ([n] (int c, int i) { return source (n + i, c); });
        };
        /* Half a second for the glide from Vocal 80 % to land. */
        play ((int) (0.5 * rate) / block + 1);

        bool same = true;
        for (int b = 0; b < 20; ++b, n += block)
        {
            a.run ([n] (int c, int i) { return source (n + i, c); });
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < block; ++i)
                    same = same && juce::exactlyEqual (a.buffer.getSample (c, i), source (n + i, c));
        }
        CHECK (same);
    }
}

TEST_CASE ("a vowel is put on the sound: Vocal up, the output is no longer the input")
{
    Instance a;
    a.set (param::vocal, 100.0);
    a.set (param::vowel, 0.0);
    juce::int64 n = 0;
    double difference = 0.0;
    for (int b = 0; b < 40; ++b, n += block)
    {
        a.run ([n] (int c, int i) { return source (n + i, c); });
        if (b >= 20)
            for (int i = 0; i < block; ++i)
                difference += std::abs (a.buffer.getSample (0, i) - source (n + i, 0));
    }
    CHECK (difference / (20 * block) > 0.01);
    CHECK (a.buffer.getMagnitude (0, block) > 0.01f);
}

TEST_CASE ("a reactivation lets go of what either engine held")
{
    for (const double mode : { 0.0, 1.0 })
    {
        CAPTURE (mode);
        Instance a;
        a.set (param::mode, mode);
        a.set (param::vocal, 100.0);
        a.set (param::resonance, 100.0);
        a.set (param::excite, 60.0);
        juce::int64 n = 0;
        for (int b = 0; b < 100; ++b, n += block)
            a.run ([n] (int c, int i) { return 2.0f * source (n + i, c); });
        CHECK (a.buffer.getMagnitude (0, block) > 0.0f);

        a.p.releaseResources();
        a.p.prepareToPlay (rate, block);
        a.run (0.0f);
        CHECK (a.buffer.getMagnitude (0, block) == 0.0f);
        CHECK (a.buffer.getMagnitude (1, 0, block) == 0.0f);
    }
}

TEST_CASE ("the host's bypass passes the audio untouched in Time")
{
    Instance a;
    a.set (param::vocal, 100.0);
    a.run (0.3f, 4, true);
    CHECK (a.buffer.getSample (0, 0) == 0.3f);
    CHECK (a.buffer.getSample (1, block - 1) == 0.3f);
}

/* JUCE's own bypass (AudioProcessor::processBypassed) passes the audio
 * through at once and asserts the latency is 0: a plugin that reports one
 * must delay its bypassed audio by it, or a host compensating for the latency
 * hears the bypassed track that much early. */
TEST_CASE ("the host's bypass in Spectral is as late as the latency reported")
{
    Instance a;
    a.set (param::mode, 1.0);
    a.settle();
    REQUIRE (pumpUntil ([&a] { return a.p.getLatencySamples() == frame; }));

    std::vector<float> out;
    for (int b = 0; b < 4; ++b)
    {
        a.run ([b] (int, int i) { return b == 0 && i == 0 ? 1.0f : 0.0f; }, 1, true);
        out.insert (out.end(), a.buffer.getReadPointer (0), a.buffer.getReadPointer (0) + block);
    }
    CHECK (out[0] == 0.0f);
    CHECK (out[(size_t) frame] == 1.0f);
}

/* ------------------------------------------------------------ the state -- */

TEST_CASE ("a saved state reopens with every value exactly")
{
    Instance a;
    a.set (param::mode, 1.0);
    a.set (param::vocal, 85.0);
    a.set (param::vowel, 1.7);
    a.set (param::size, 30.0);
    a.set (param::resonance, 65.0);
    a.set (param::excite, 40.0);
    a.set (param::mix, 90.0);
    a.set (param::output, -3.0);
    a.set (param::voice, 40.0);
    a.set (param::ceiling, 0.0);
    juce::MemoryBlock state;
    a.p.getStateInformation (state);

    Instance b;
    b.p.setStateInformation (state.getData(), (int) state.getSize());
    for (int i = 0; i < Processor::paramCount; ++i)
    {
        CHECK_MESSAGE (b.p.plain (i) == a.p.plain (i), a.p.parameter (i).getParameterID());
        CHECK (b.p.parameter (i).getCurrentValueAsText() == a.p.parameter (i).getCurrentValueAsText());
    }
    CHECK (b.p.plain (param::vowel) == 1.7);
    CHECK (b.p.plain (param::mode) == 1.0);

    /* What no build of this plugin wrote changes nothing. */
    const char junk[] = "not a state";
    b.p.setStateInformation (junk, (int) sizeof junk);
    CHECK (b.p.plain (param::vowel) == 1.7);
}

/* ------------------------------------------------------------ the model -- */

TEST_CASE ("the model: the formants are the engine's, at Size and Resonance now")
{
    Instance a;
    a.set (param::size, 30.0);
    a.set (param::resonance, 65.0);
    for (const double v : { 0.0, 1.3, 2.0, 3.55, 4.0 })
    {
        CAPTURE (v);
        /* No voice captured: the textbook's, whatever Voice says. */
        double want[VZ_FORMANTS] {};
        vz_formants (nullptr, 100.0, v, 30.0, 65.0, want);
        const auto f = a.p.formants (v);
        for (int k = 0; k < VZ_FORMANTS; ++k)
            CHECK (f[(size_t) k] == want[k]);
    }
    /* Moved by Size the moment it moves: no audio needed. */
    const auto before = a.p.formants (1.0);
    a.set (param::size, -60.0);
    CHECK (a.p.formants (1.0)[0] > before[0]);

    CHECK (a.p.bipolar (param::size));
    CHECK (a.p.bipolar (param::output));
    CHECK_FALSE (a.p.bipolar (param::vocal));
    CHECK_FALSE (a.p.bipolar (param::vowel));
    CHECK_FALSE (a.p.bipolar (param::mode));
    CHECK_FALSE (a.p.bipolar (param::voice));
    CHECK_FALSE (a.p.bipolar (99));
}

TEST_CASE ("the model: the picture waits for audio, then says which engine drew it")
{
    Instance a;
    a.p.editorOpened();
    CHECK (a.p.picture().count() == 0);

    juce::int64 n = 0;
    const auto play = [&a, &n] (int blocks)
    {
        for (int b = 0; b < blocks; ++b, n += block)
            a.run ([n] (int c, int i) { return source (n + i, c); });
    };
    play (100);
    {
        const auto& pic = a.p.picture();
        REQUIRE (pic.count() == VZ_POINTS);
        CHECK (pic.serial != 0);
        CHECK_FALSE (pic.spectral);
        CHECK (pic.latency == 0);
        CHECK (pic.sampleRate == rate);
        CHECK (pic.hz.front() == doctest::Approx (VZ_LOWEST_HZ));
        CHECK (pic.hz.back() == doctest::Approx (VZ_HIGHEST_HZ));
        CHECK (std::is_sorted (pic.hz.begin(), pic.hz.end()));
        /* Each curve with its peak at 0 dB. */
        CHECK (*std::max_element (pic.input.begin(), pic.input.end()) == doctest::Approx (0.0));
        CHECK (*std::max_element (pic.output.begin(), pic.output.end()) == doctest::Approx (0.0));
        CHECK (*std::min_element (pic.output.begin(), pic.output.end()) < -3.0);
    }
    const auto serial = a.p.picture().serial;
    play (10);
    CHECK (a.p.picture().serial != serial);

    a.set (param::mode, 1.0);
    play (100);
    const auto& pic = a.p.picture();
    CHECK (pic.spectral);
    CHECK (pic.latency == frame);
    CHECK (pic.latencyMs() == doctest::Approx (1000.0 * frame / rate));
    a.p.editorClosed();
}

TEST_CASE ("the model: the envelopes are worked out only while a window watches; the rest always")
{
    Instance a;
    juce::int64 n = 0;
    const auto play = [&a, &n] (int blocks)
    {
        for (int b = 0; b < blocks; ++b, n += block)
            a.run ([n] (int c, int i) { return source (n + i, c); });
    };
    const auto flat = [] (const std::vector<double>& curve)
    { return std::all_of (curve.begin(), curve.end(), [] (double x) { return juce::exactlyEqual (x, 0.0); }); };

    /* No window: the picture says what runs, and draws nothing. */
    play (100);
    {
        const auto& pic = a.p.picture();
        REQUIRE (pic.count() == VZ_POINTS);
        CHECK (pic.serial != 0);
        CHECK (pic.sampleRate == rate);
        CHECK (flat (pic.input));
        CHECK (flat (pic.output));
    }

    /* A window opens: the envelopes come. */
    a.p.editorOpened();
    play (20);
    const auto watched = a.p.picture().output;
    CHECK_FALSE (flat (watched));

    /* It closes: the envelopes stay as they were, while the rest goes on --
     * the serial, and the engine running and its latency. */
    a.p.editorClosed();
    play (5);
    const auto serial = a.p.picture().serial;
    a.set (param::vowel, 3.0);
    a.set (param::mode, 1.0);
    play (100);
    const auto& pic = a.p.picture();
    CHECK (pic.serial != serial);
    CHECK (pic.spectral);
    CHECK (pic.latency == frame);
    CHECK (pic.output == watched);
}

/* ------------------------------------------------------------ the voice -- */

namespace
{
using ni::vz::Voice;
using Status = ni::vz::Voice::Status;

/* The singer every capture here hears: A, I and U at 140 Hz, 0.6 s each, a
 * voice 1.15 times the textbook's -- E and O never sung. */
constexpr double singerScale = 1.15;
const std::vector<float>& singer()
{
    static const auto x = ni::vz::test::sing ({ 0, 2, 4 }, singerScale, 140.0, 0.6, rate);
    return x;
}

/* What the engine measures from `x`, asked directly: how it ended, and the
 * voice when there is one. */
std::pair<VzCaptured, VzVoice> measure (const std::vector<float>& x)
{
    const std::unique_ptr<VzCapture, void (*) (VzCapture*)> job (vz_capture_new(), vz_capture_free);
    VzVoice v {};
    const auto how = vz_capture_run (job.get(), x.data(), x.size(), rate, &v);
    return { how, v };
}

/* What the engine measures from the singer. */
VzVoice measured()
{
    const auto [how, v] = measure (singer());
    REQUIRE (how == VzCaptured_Voice);
    return v;
}

/* Sung, but no vowel: the singer's pulses through formants at 1500, 1700,
 * 2500, 3500 and 4500 Hz -- F1 and F2 far from any vowel's. */
const std::vector<float>& noVowel()
{
    static const auto x = ni::vz::test::singFormants ({ { 1500.0, 1700.0, 2500.0, 3500.0, 4500.0 } }, 140.0, 1.5, rate);
    return x;
}

/* A take long enough that its capture is still running when it is called
 * off: the singer over and over, for `seconds`. */
std::vector<float> longTake (double seconds)
{
    std::vector<float> x;
    while (x.size() < (size_t) (seconds * rate))
        x.insert (x.end(), singer().begin(), singer().end());
    return x;
}

/* The message loop runs until a capture has answered, for 20 s at most. */
bool awaitCapture (Processor& p)
{
    const auto until = juce::Time::getMillisecondCounter() + 20000;
    while (p.voice().status == Status::capturing && juce::Time::getMillisecondCounter() < until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return p.voice().status != Status::capturing;
}

/* The textbook's formants at `vowel`, Size and Resonance as `p` holds them. */
ni::vz::Formants textbookAt (Processor& p, double vowel)
{
    ni::vz::Formants f {};
    vz_formants (nullptr, 100.0, vowel, p.plain (param::size), p.plain (param::resonance), f.data());
    return f;
}

/* A state as XML, and back. */
std::unique_ptr<juce::XmlElement> xmlOf (const juce::MemoryBlock& state)
{
    return juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
}

juce::MemoryBlock binaryOf (const juce::XmlElement& xml)
{
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary (xml, out);
    return out;
}

juce::MemoryBlock stateOf (Processor& p)
{
    juce::MemoryBlock state;
    p.getStateInformation (state);
    return state;
}

void load (Processor& p, const juce::MemoryBlock& state)
{
    p.setStateInformation (state.getData(), (int) state.getSize());
}

/* A processor that has captured the singer, from `file`. */
void captureSinger (Processor& p, const juce::File& file)
{
    REQUIRE (ni::vz::test::writeWav (file, singer(), rate));
    p.capture (file);
    REQUIRE (awaitCapture (p));
    REQUIRE (p.voice().status == Status::captured);
}

/* The local peak of `out` between `lo` and `hi` Hz, by the picture's
 * frequencies: where a formant is drawn. */
double peakHz (const ni::vz::Picture& pic, double lo, double hi)
{
    double best = -1.0e9, at = 0.0;
    for (int i = 1; i + 1 < pic.count(); ++i)
    {
        const auto k = (size_t) i;
        if (pic.hz[k] < lo || pic.hz[k] > hi)
            continue;
        if (pic.output[k] > pic.output[k - 1] && pic.output[k] >= pic.output[k + 1] && pic.output[k] > best)
        {
            best = pic.output[k];
            at = pic.hz[k];
        }
    }
    return at;
}
} // namespace

TEST_CASE ("a recording captures a voice: the vowels sung are measured, the rest filled in for that singer")
{
    ni::vz::test::Scratch scratch ("vz_processor");
    Instance a;
    HostListener host;
    a.p.addListener (&host);
    CHECK (a.p.voice().status == Status::none);
    CHECK (a.p.recordingWildcard().contains ("*.wav"));
    CHECK (a.p.recordingWildcard().contains ("*.aif"));

    const auto file = scratch / "My Voice.wav";
    REQUIRE (ni::vz::test::writeWav (file, singer(), rate));
    const auto before = a.p.voice().serial;
    a.p.capture (file);
    /* Under way at once, on a thread of its own. */
    CHECK (a.p.voice().status == Status::capturing);
    CHECK (a.p.voice().name == "My Voice.wav");
    CHECK (a.p.voice().serial != before);
    REQUIRE (awaitCapture (a.p));

    const auto v = a.p.voice();
    CHECK (v.status == Status::captured);
    CHECK (v.name == "My Voice.wav");
    CHECK (v.found == std::array<bool, 5> { true, false, true, false, true });
    /* Captured by hand: the set is unsaved. */
    CHECK (host.unsaved >= 1);

    /* At Voice 100 the singer's vowels: the engine's measurement, exactly,
     * and near enough the voice that was sung. */
    const auto direct = measured();
    for (const int vowel : { 0, 2, 4 })
    {
        CAPTURE (vowel);
        double want[VZ_FORMANTS] {};
        vz_formants (&direct, 100.0, vowel, 0.0, 50.0, want);
        const auto f = a.p.formants (vowel);
        for (int k = 0; k < VZ_FORMANTS; ++k)
            CHECK (f[(size_t) k] == want[k]);
        CHECK (f[0] / (ni::vz::test::textbook[vowel][0] * singerScale) == doctest::Approx (1.0).epsilon (0.1));
        CHECK (f[1] / (ni::vz::test::textbook[vowel][1] * singerScale) == doctest::Approx (1.0).epsilon (0.06));
    }
    /* E, never sung: the textbook's, scaled to the singer. */
    CHECK (a.p.formants (1.0)[1] == doctest::Approx (ni::vz::test::textbook[1][1] * direct.scale));

    /* At Voice 0 the textbook's, exactly; between, between. */
    a.set (param::voice, 0.0);
    for (const double vowel : { 0.0, 1.5, 4.0 })
        CHECK (a.p.formants (vowel) == textbookAt (a.p, vowel));
    a.set (param::voice, 50.0);
    const auto half = a.p.formants (0.0)[1];
    CHECK (half > textbookAt (a.p, 0.0)[1]);
    a.set (param::voice, 100.0);
    CHECK (half < a.p.formants (0.0)[1]);
    a.p.removeListener (&host);
}

TEST_CASE ("the engine sings the captured voice: its vowel drawn where it was sung, and none of it at Voice 0")
{
    ni::vz::test::Scratch scratch ("vz_processor");
    const auto file = scratch / "singer.wav";

    /* Vowel A, all the way: the picture's curve is the vowel's filter --
     * drawn while a window watches. */
    Instance sung, book;
    captureSinger (sung.p, file);
    for (auto* i : { &sung, &book })
    {
        i->p.editorOpened();
        i->set (param::vocal, 100.0);
        i->set (param::vowel, 0.0);
    }
    juce::int64 n = 0;
    for (int b = 0; b < 100; ++b, n += block)
        for (auto* i : { &sung, &book })
            i->run ([n] (int c, int s) { return source (n + s, c); });
    const double f2Book = peakHz (book.p.picture(), 1000.0, 2200.0);
    const double f2Sung = peakHz (sung.p.picture(), 1000.0, 2200.0);
    CAPTURE (f2Book);
    CAPTURE (f2Sung);
    CHECK (f2Book == doctest::Approx (ni::vz::test::textbook[0][1]).epsilon (0.06));
    CHECK (f2Sung / f2Book == doctest::Approx (singerScale).epsilon (0.08));

    /* At Voice 0 a captured voice is the textbook's, bit for bit, once the
     * glide from Voice's default 100 % has landed and what it left in the
     * filters has died away -- the first second is not compared; at 100 %
     * it is not the textbook's. */
    for (const double amount : { 0.0, 100.0 })
    {
        CAPTURE (amount);
        Instance withVoice, without;
        captureSinger (withVoice.p, file);
        for (auto* i : { &withVoice, &without })
        {
            i->set (param::vocal, 100.0);
            i->set (param::vowel, 1.5);
            i->set (param::voice, amount);
        }
        bool same = true;
        juce::int64 m = 0;
        for (int b = 0; b < 200; ++b, m += block)
        {
            withVoice.run ([m] (int c, int s) { return source (m + s, c); });
            without.run ([m] (int c, int s) { return source (m + s, c); });
            if (b < 100)
                continue;
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < block; ++s)
                    same = same && juce::exactlyEqual (withVoice.buffer.getSample (c, s), without.buffer.getSample (c, s));
        }
        CHECK (same == (amount == 0.0));
    }
}

TEST_CASE ("a saved set keeps the voice -- fifty numbers, not the recording -- and reopens it")
{
    ni::vz::test::Scratch scratch ("vz_processor");
    Instance a;
    captureSinger (a.p, scratch / "My Voice.wav");
    const auto state = stateOf (a.p);

    const auto xml = xmlOf (state);
    REQUIRE (xml != nullptr);
    const auto* voice = xml->getChildByName ("voice");
    REQUIRE (voice != nullptr);
    CHECK (voice->getStringAttribute ("name") == "My Voice.wav");
    CHECK (voice->getNumChildElements() == 5);
    const char* names[] { "A", "E", "I", "O", "U" };
    int row = 0;
    for (const auto* vowel : voice->getChildWithTagNameIterator ("vowel"))
    {
        CAPTURE (row);
        CHECK (vowel->getStringAttribute ("name") == names[row]);
        CHECK (vowel->getIntAttribute ("found") == (row % 2 == 0 ? 1 : 0));
        CHECK (juce::StringArray::fromTokens (vowel->getStringAttribute ("freq"), " ", "").size() == 5);
        CHECK (juce::StringArray::fromTokens (vowel->getStringAttribute ("bandwidth"), " ", "").size() == 5);
        ++row;
    }
    CHECK (state.getSize() < 4096);

    /* A second instance -- a set reopened -- sings the same voice, to the
     * six decimals written, and is not marked unsaved for it. */
    Instance b;
    HostListener host;
    b.p.addListener (&host);
    load (b.p, state);
    CHECK (host.unsaved == 0);
    const auto v = b.p.voice();
    CHECK (v.status == Status::captured);
    CHECK (v.name == "My Voice.wav");
    CHECK (v.found == a.p.voice().found);
    for (const double vowel : { 0.0, 1.0, 2.0, 2.5, 4.0 })
        for (int k = 0; k < VZ_FORMANTS; ++k)
            CHECK (b.p.formants (vowel)[(size_t) k] == doctest::Approx (a.p.formants (vowel)[(size_t) k]).epsilon (1.0e-8));
    /* And saved again, the very same voice. */
    const auto again = xmlOf (stateOf (b.p));
    REQUIRE (again != nullptr);
    REQUIRE (again->getChildByName ("voice") != nullptr);
    CHECK (again->getChildByName ("voice")->isEquivalentTo (voice, false));
    b.p.removeListener (&host);
}

TEST_CASE ("a set without a voice, or with one that is not a voice, forgets the voice held")
{
    ni::vz::test::Scratch scratch ("vz_processor");
    Instance a;
    captureSinger (a.p, scratch / "My Voice.wav");
    const auto good = stateOf (a.p);

    /* `edit` done to the saved voice, the result loaded over the voice. */
    const auto loadEdited = [&good] (const std::function<void (juce::XmlElement&)>& edit)
    {
        auto xml = xmlOf (good);
        REQUIRE (xml != nullptr);
        auto* voice = xml->getChildByName ("voice");
        REQUIRE (voice != nullptr);
        edit (*voice);
        Instance b;
        load (b.p, good);
        REQUIRE (b.p.voice().status == Status::captured);
        load (b.p, binaryOf (*xml));
        return std::make_pair (b.p.voice(), b.p.formants (0.0) == textbookAt (b.p, 0.0));
    };
    const auto row = [] (juce::XmlElement& voice, int i) { return voice.getChildElement (i); };

    SUBCASE ("no <voice> at all: a set saved before the capture")
    {
        Instance fresh;
        Instance b;
        load (b.p, good);
        load (b.p, stateOf (fresh.p));
        CHECK (b.p.voice().status == Status::none);
        CHECK (b.p.voice().name.isEmpty());
        CHECK (b.p.formants (0.0) == textbookAt (b.p, 0.0));
        CHECK (xmlOf (stateOf (b.p))->getChildByName ("voice") == nullptr);
    }
    SUBCASE ("a number that is not a number")
    {
        const auto [v, textbook] = loadEdited ([&row] (juce::XmlElement& voice)
                                               { row (voice, 1)->setAttribute ("freq", "nan 2089 2691 3500 4500"); });
        CHECK (v.status == Status::none);
        CHECK (textbook);
    }
    SUBCASE ("an infinite bandwidth")
    {
        const auto [v, textbook] = loadEdited ([&row] (juce::XmlElement& voice)
                                               { row (voice, 3)->setAttribute ("bandwidth", "60 90 inf 200 250"); });
        CHECK (v.status == Status::none);
        CHECK (textbook);
    }
    SUBCASE ("words where the numbers were")
    {
        const auto [v, textbook] = loadEdited ([&row] (juce::XmlElement& voice)
                                               { row (voice, 0)->setAttribute ("freq", "low middle high higher highest"); });
        CHECK (v.status == Status::none);
        CHECK (textbook);
    }
    SUBCASE ("a row short of a number")
    {
        const auto [v, textbook] = loadEdited ([&row] (juce::XmlElement& voice)
                                               { row (voice, 2)->setAttribute ("freq", "342 2322 3000 3500"); });
        CHECK (v.status == Status::none);
        CHECK (textbook);
    }
    SUBCASE ("a vowel missing")
    {
        const auto [v, textbook] = loadEdited ([&row] (juce::XmlElement& voice)
                                               { voice.removeChildElement (row (voice, 4), true); });
        CHECK (v.status == Status::none);
        CHECK (textbook);
    }
    SUBCASE ("a format newer than this build reads")
    {
        CHECK (xmlOf (good)->getChildByName ("voice")->getIntAttribute ("format") == 1);
        const auto [v, textbook] = loadEdited ([] (juce::XmlElement& voice) { voice.setAttribute ("format", 2); });
        CHECK (v.status == Status::none);
        CHECK (textbook);
    }
}

TEST_CASE ("forgetting the voice by hand sings the textbook's again, and marks the set unsaved")
{
    ni::vz::test::Scratch scratch ("vz_processor");
    Instance a;
    captureSinger (a.p, scratch / "My Voice.wav");
    HostListener host;
    a.p.addListener (&host);
    const auto serial = a.p.voice().serial;
    a.p.forgetVoice();
    CHECK (a.p.voice().status == Status::none);
    CHECK (a.p.voice().name.isEmpty());
    CHECK (a.p.voice().found == std::array<bool, 5> {});
    CHECK (a.p.voice().serial != serial);
    CHECK (a.p.formants (2.0) == textbookAt (a.p, 2.0));
    CHECK (host.unsaved == 1);
    CHECK (xmlOf (stateOf (a.p))->getChildByName ("voice") == nullptr);
    a.p.removeListener (&host);
}

TEST_CASE ("what cannot be read, what has no singing in it and what has no vowels capture nothing, and say which")
{
    ni::vz::test::Scratch scratch ("vz_processor");
    Instance a;

    const auto notes = scratch / "notes.wav";
    REQUIRE (notes.replaceWithText ("a shopping list, not a recording"));
    a.p.capture (notes);
    REQUIRE (awaitCapture (a.p));
    CHECK (a.p.voice().status == Status::unreadable);
    CHECK (a.p.voice().name == "notes.wav");

    a.p.capture (scratch / "nowhere.wav");
    REQUIRE (awaitCapture (a.p));
    CHECK (a.p.voice().status == Status::unreadable);

    const auto silence = scratch / "silence.wav";
    REQUIRE (ni::vz::test::writeWav (silence, std::vector<float> ((size_t) rate, 0.0f), rate));
    a.p.capture (silence);
    REQUIRE (awaitCapture (a.p));
    CHECK (a.p.voice().status == Status::unsung);
    CHECK (a.p.voice().name == "silence.wav");

    /* Sung, clearly -- a voice's pulses -- but through resonances at 1500
     * and 1700 Hz, near no vowel at all. */
    const auto hum = scratch / "hum.wav";
    REQUIRE (ni::vz::test::writeWav (hum, noVowel(), rate));
    a.p.capture (hum);
    REQUIRE (awaitCapture (a.p));
    CHECK (a.p.voice().status == Status::noVowels);
    CHECK (a.p.voice().name == "hum.wav");
    CHECK_FALSE (a.p.voice().sung);

    CHECK (a.p.formants (0.0) == textbookAt (a.p, 0.0));
    CHECK (xmlOf (stateOf (a.p))->getChildByName ("voice") == nullptr);
}

TEST_CASE ("a capture that fails after one that worked: what is sung is what is shown")
{
    ni::vz::test::Scratch scratch ("vz_processor");
    Instance a;
    captureSinger (a.p, scratch / "My Voice.wav");
    const auto notes = scratch / "notes.wav";
    REQUIRE (notes.replaceWithText ("not a recording"));
    a.p.capture (notes);
    REQUIRE (awaitCapture (a.p));
    CHECK (a.p.voice().status == Status::unreadable);

    /* The voice before is still sung, and said to be -- under its own name,
     * in the editor and in the set -- so it can still be forgotten; the
     * failure is the latest capture's, and named for its recording. */
    CHECK (a.p.voice().name == "notes.wav");
    CHECK (a.p.voice().sung);
    CHECK (a.p.voice().sungName == "My Voice.wav");
    const auto xml = xmlOf (stateOf (a.p));
    const auto* voice = xml->getChildByName ("voice");
    REQUIRE (voice != nullptr);
    CHECK (voice->getStringAttribute ("name") == "My Voice.wav");
    CHECK (a.p.formants (0.0) != textbookAt (a.p, 0.0));
    a.p.forgetVoice();
    CHECK (! a.p.voice().sung);
    CHECK (a.p.formants (0.0) == textbookAt (a.p, 0.0));
}

TEST_CASE ("a second capture while one runs is ignored")
{
    ni::vz::test::Scratch scratch ("vz_processor");
    Instance a;
    const auto first = scratch / "first.wav";
    const auto second = scratch / "second.wav";
    REQUIRE (ni::vz::test::writeWav (first, singer(), rate));
    REQUIRE (ni::vz::test::writeWav (second, std::vector<float> ((size_t) rate, 0.0f), rate));
    a.p.capture (first);
    const auto serial = a.p.voice().serial;
    a.p.capture (second);
    CHECK (a.p.voice().name == "first.wav");
    CHECK (a.p.voice().serial == serial);
    REQUIRE (awaitCapture (a.p));
    CHECK (a.p.voice().status == Status::captured);
    CHECK (a.p.voice().name == "first.wav");
}

TEST_CASE ("each vowel's confidence reaches the model and the set, and comes back with it")
{
    ni::vz::test::Scratch scratch ("vz_processor");
    Instance a;
    captureSinger (a.p, scratch / "My Voice.wav");
    const auto direct = measured();
    const auto v = a.p.voice();
    for (size_t k = 0; k < 5; ++k)
    {
        CAPTURE (k);
        CHECK (v.confidence[k] == direct.confidence[k]);
        if (v.found[k])
        {
            CHECK (v.confidence[k] > 0.0);
            CHECK (v.confidence[k] <= 1.0);
        }
        else
        {
            CHECK (v.confidence[k] == 0.0);
        }
    }

    /* Written to three places, and read back so. */
    const auto state = stateOf (a.p);
    const auto xml = xmlOf (state);
    REQUIRE (xml != nullptr);
    REQUIRE (xml->getChildByName ("voice") != nullptr);
    int row = 0;
    for (const auto* vowel : xml->getChildByName ("voice")->getChildWithTagNameIterator ("vowel"))
    {
        CAPTURE (row);
        CHECK (std::abs (vowel->getDoubleAttribute ("confidence") - v.confidence[(size_t) row]) <= 0.0005);
        ++row;
    }
    Instance b;
    load (b.p, state);
    for (size_t k = 0; k < 5; ++k)
        CHECK (std::abs (b.p.voice().confidence[k] - v.confidence[k]) <= 0.0005);
}

TEST_CASE ("five vowels sung in order are told apart by their order, and the set says so")
{
    ni::vz::test::Scratch scratch ("vz_processor");

    /* A, E, I, O, U, one after the other: five stretches. */
    const auto five = ni::vz::test::sing ({ 0, 1, 2, 3, 4 }, singerScale, 140.0, 0.6, rate);
    const auto [how, direct] = measure (five);
    REQUIRE (how == VzCaptured_Voice);
    CHECK (direct.by_order);
    CHECK_FALSE (measured().by_order);

    const auto file = scratch / "AEIOU.wav";
    REQUIRE (ni::vz::test::writeWav (file, five, rate));
    Instance a;
    a.p.capture (file);
    REQUIRE (awaitCapture (a.p));
    REQUIRE (a.p.voice().status == Status::captured);
    CHECK (a.p.voice().found == std::array<bool, 5> { true, true, true, true, true });
    const auto state = stateOf (a.p);
    CHECK (xmlOf (state)->getChildByName ("voice")->getIntAttribute ("order") == 1);

    /* Reopened and saved again, still by order. */
    Instance b;
    load (b.p, state);
    CHECK (xmlOf (stateOf (b.p))->getChildByName ("voice")->getIntAttribute ("order") == 1);

    /* The singer of A, I and U alone: three stretches, by likeness. */
    Instance c;
    captureSinger (c.p, scratch / "AIU.wav");
    CHECK (xmlOf (stateOf (c.p))->getChildByName ("voice")->getIntAttribute ("order") == 0);
}

TEST_CASE ("Forget keeps the voice for one Restore -- until another voice is sung or a set is read")
{
    ni::vz::test::Scratch scratch ("vz_processor");
    Instance a;
    captureSinger (a.p, scratch / "My Voice.wav");
    const auto sung = a.p.formants (0.0);
    CHECK_FALSE (a.p.voice().restorable);

    HostListener host;
    a.p.addListener (&host);
    a.p.forgetVoice();
    CHECK_FALSE (a.p.voice().sung);
    CHECK (a.p.voice().restorable);
    CHECK (a.p.formants (0.0) == textbookAt (a.p, 0.0));

    a.p.restoreVoice();
    CHECK (a.p.voice().sung);
    CHECK (a.p.voice().sungName == "My Voice.wav");
    CHECK (a.p.voice().status == Status::captured);
    CHECK (a.p.voice().found == std::array<bool, 5> { true, false, true, false, true });
    CHECK_FALSE (a.p.voice().restorable);
    CHECK (a.p.formants (0.0) == sung);
    /* Both by hand: the set marked unsaved each time. */
    CHECK (host.unsaved == 2);
    a.p.removeListener (&host);

    /* A restore with nothing to restore does nothing. */
    const auto serial = a.p.voice().serial;
    a.p.restoreVoice();
    CHECK (a.p.voice().serial == serial);

    /* Forgotten, then a new voice sung: the old one is gone for good. */
    a.p.forgetVoice();
    REQUIRE (a.p.voice().restorable);
    const auto five = scratch / "AEIOU.wav";
    REQUIRE (ni::vz::test::writeWav (five, ni::vz::test::sing ({ 0, 1, 2, 3, 4 }, 0.95, 120.0, 0.6, rate), rate));
    a.p.capture (five);
    REQUIRE (awaitCapture (a.p));
    REQUIRE (a.p.voice().sung);
    CHECK (a.p.voice().sungName == "AEIOU.wav");
    CHECK_FALSE (a.p.voice().restorable);
    a.p.restoreVoice();
    CHECK (a.p.voice().sungName == "AEIOU.wav");

    /* Forgotten, then a set read: nothing to restore either. */
    a.p.forgetVoice();
    REQUIRE (a.p.voice().restorable);
    Instance fresh;
    load (a.p, stateOf (fresh.p));
    CHECK_FALSE (a.p.voice().restorable);
    a.p.restoreVoice();
    CHECK_FALSE (a.p.voice().sung);
}

TEST_CASE ("a set read while a capture runs wins: the capture's answer is dropped, and nothing says capturing")
{
    ni::vz::test::Scratch scratch ("vz_processor");
    const auto take = scratch / "long.wav";
    REQUIRE (ni::vz::test::writeWav (take, longTake (20.0), rate));

    SUBCASE ("a set without a voice")
    {
        Instance a;
        /* capture() says capturing at once, and its answer comes back only
         * through the message loop, which does not run before the load: so
         * the capture is still under way when the set is read, however fast
         * the machine analyses the take. */
        a.p.capture (take);
        REQUIRE (a.p.voice().status == Status::capturing);
        Instance fresh;
        load (a.p, stateOf (fresh.p));
        CHECK (a.p.voice().status == Status::none);
        /* Long enough for the capture to have answered, had it not been
         * called off. */
        juce::MessageManager::getInstance()->runDispatchLoopUntil (3000);
        CHECK (a.p.voice().status == Status::none);
        CHECK_FALSE (a.p.voice().sung);
        CHECK (a.p.formants (0.0) == textbookAt (a.p, 0.0));

        /* And a capture can be started again. */
        captureSinger (a.p, scratch / "My Voice.wav");
        CHECK (a.p.voice().sungName == "My Voice.wav");
    }
    SUBCASE ("a set with a voice of its own")
    {
        Instance other;
        captureSinger (other.p, scratch / "From The Set.wav");
        const auto set = stateOf (other.p);

        Instance a;
        /* capture() says capturing at once, and its answer comes back only
         * through the message loop, which does not run before the load: so
         * the capture is still under way when the set is read, however fast
         * the machine analyses the take. */
        a.p.capture (take);
        REQUIRE (a.p.voice().status == Status::capturing);
        load (a.p, set);
        CHECK (a.p.voice().status == Status::captured);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (3000);
        CHECK (a.p.voice().status == Status::captured);
        CHECK (a.p.voice().sungName == "From The Set.wav");
    }
}

TEST_CASE ("a processor closed while a capture runs calls it off, and closes at once")
{
    ni::vz::test::Scratch scratch ("vz_processor");
    const auto file = scratch / "long.wav";
    /* The longest recording a capture reads: two minutes. */
    REQUIRE (ni::vz::test::writeWav (file, longTake (Processor::longestRecordingS), rate));

    auto p = std::make_unique<Processor>();
    p->prepareToPlay (rate, block);
    p->capture (file);
    /* Well into it: reading, or analysing. */
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    REQUIRE (p->voice().status == Status::capturing);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    p.reset();
    const auto took = juce::Time::getMillisecondCounterHiRes() - start;
    CAPTURE (took);
    CHECK (took < 1000.0);
    /* Its answer, if one was posted, reaches no one. */
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
}

/* ---------------------------------------------------------- the ceiling -- */

TEST_CASE ("Ceiling: nothing passes full scale with it on; with it off, what is driven past it passes")
{
    for (const bool on : { true, false })
    {
        CAPTURE (on);
        Instance a;
        a.set (param::vocal, 0.0);
        a.set (param::output, 12.0);
        a.set (param::ceiling, on ? 1.0 : 0.0);
        float peak = 0.0f;
        for (int b = 0; b < 100; ++b)
        {
            a.run ([b] (int c, int i)
                   { return 0.9f * (float) std::sin (0.013 * (double) (b * block + i) + 0.5 * c); });
            if (b >= 50)
                peak = std::max (peak, a.buffer.getMagnitude (0, block));
        }
        CAPTURE (peak);
        if (on)
        {
            CHECK (peak <= 1.0f);
            /* Bent, not cut at the knee. */
            CHECK (peak > 0.9660508789898133f);
        }
        else
        {
            CHECK (peak > 3.0f);
        }
    }
}

/* ----------------------------------------------------------- the bypass -- */

TEST_CASE ("after a bypass the engine resumes from what it heard, never from what it held")
{
    for (const double mode : { 0.0, 1.0 })
    {
        CAPTURE (mode);
        Instance a;
        a.set (param::mode, mode);
        a.set (param::vocal, 100.0);
        a.set (param::resonance, 100.0);
        a.set (param::excite, 60.0);
        REQUIRE (pumpUntil ([&a] { return a.p.getLatencySamples() == a.p.latencyForMode(); }));
        juce::int64 n = 0;
        for (int b = 0; b < 100; ++b, n += block)
            a.run ([n] (int c, int i) { return 2.0f * source (n + i, c); });
        REQUIRE (a.buffer.getMagnitude (0, block) > 0.0f);

        /* A second of silence, bypassed; then silence played. */
        a.run (0.0f, 100, true);
        a.run (0.0f);
        CHECK (a.buffer.getMagnitude (0, block) == 0.0f);
        CHECK (a.buffer.getMagnitude (1, 0, block) == 0.0f);
    }
}

TEST_CASE ("after a bypass the sound comes back in time, from the audio the bypass passed")
{
    /* Time, at Vocal 0, is the input; Spectral, at Mix 0, the input one
     * frame late. Either way, once the 10 ms fade-in is over, the first
     * block after a bypass is the input exactly as late as the latency --
     * the bypass's audio included. */
    for (const double mode : { 0.0, 1.0 })
    {
        CAPTURE (mode);
        Instance a;
        a.set (param::mode, mode);
        a.set (param::vocal, 0.0);
        a.set (param::mix, mode > 0.5 ? 0.0 : 100.0);
        REQUIRE (pumpUntil ([&a] { return a.p.getLatencySamples() == a.p.latencyForMode(); }));
        const int latency = a.p.getLatencySamples();
        juce::int64 n = 0;
        const auto play = [&a, &n] (int blocks, bool bypassed)
        {
            for (int b = 0; b < blocks; ++b, n += block)
                a.run ([n] (int c, int i) { return source (n + i, c); }, 1, bypassed);
        };
        play (100, false);
        play (50, true);

        /* The first blocks after: the input, as late as the latency, from
         * the end of the fade (10 ms, 480 samples) on. */
        const int fade = (int) (0.01 * rate);
        bool same = true;
        for (int b = 0; b < 4; ++b)
        {
            const auto start = n;
            play (1, false);
            for (int c = 0; c < 2; ++c)
                for (int i = b == 0 ? fade : 0; i < block; ++i)
                    same = same && juce::exactlyEqual (a.buffer.getSample (c, i), source (start + i - latency, c));
        }
        CHECK (same);
    }
}


/* ----------------------------------------------------------- the window -- */

TEST_CASE ("the window opens at the design's size")
{
    Instance a;
    std::unique_ptr<juce::AudioProcessorEditor> editor (a.p.createEditorAndMakeActive());
    REQUIRE (editor != nullptr);
    CHECK (editor->getWidth() == ni::vz::Editor::designWidth);
    CHECK (editor->getHeight() == ni::vz::Editor::designHeight);
    a.run (0.2f, 4);
    editor.reset();
}

TEST_CASE ("on the real model the envelope names the engine drawing it, and its latency")
{
    Instance a;
    ni::vz::Editor editor (a.p);
    auto& plot = editor.envelopePlot();
    CHECK_FALSE (plot.hasPicture());

    juce::int64 n = 0;
    for (int b = 0; b < 40; ++b, n += block)
        a.run ([n] (int c, int i) { return source (n + i, c); });
    editor.tick (0.0);
    CHECK (plot.hasPicture());
    CHECK (plot.getCaption() == "ENVELOPE   TIME");

    a.set (param::mode, 1.0);
    for (int b = 0; b < 40; ++b, n += block)
        a.run ([n] (int c, int i) { return source (n + i, c); });
    editor.tick (0.0);
    /* 1024 samples at 48 kHz: 21.3 ms. */
    CHECK (plot.getCaption() == "ENVELOPE   SPECTRAL   21 MS LATE");
    CHECK (editor.vowelChart().getCaption() == "VOWEL   A");
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    doctest::Context context (argc, argv);
    return context.run();
}

TEST_CASE ("the state saved at version 1 loads; a newer one is refused; a missing value is its default")
{
    ni::test::checkStateFixture (
        [] {
            auto p = std::make_unique<Processor>();
            p->prepareToPlay (48000.0, 512);
            return std::unique_ptr<juce::AudioProcessor> (std::move (p));
        },
        juce::File (NI_FIXTURES).getChildFile ("vocalize/v1.state"),
        { { "vocal", 40.0 }, { "mode", 1.0 }, { "vowel", 2.0 }, { "size", -50.0 }, { "excite", 25.0 },
          { "output", -3.0 }, { "ceiling", 0.0 } });
}
