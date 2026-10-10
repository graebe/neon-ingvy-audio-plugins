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
 *   the model       the formants the engine's, at Size and Resonance now;
 *                   the picture after audio, saying which engine drew it;
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

/* What the host is told: every latency change it was notified of. */
struct HostListener final : juce::AudioProcessorListener
{
    int latencyChanges = 0;

    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override
    {
        latencyChanges += d.latencyChanged ? 1 : 0;
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
        VzParamInfo info {};
        REQUIRE (vz_param_info (i, &info));
        CHECK (prm.getParameterIndex() == i);
        CHECK (prm.getParameterID() == engineText (vz_param_key, i));
        CHECK (prm.getName (32) == engineText (vz_param_name, i));
        CHECK (prm.convertFrom0to1 (0.0f) == (float) info.min);
        CHECK (prm.convertFrom0to1 (1.0f) == (float) info.max);
        CHECK (a.p.plain (i) == info.default_value);
        ids.insert (prm.getParameterID());
    }
    CHECK (ids == std::set<juce::String> { "mode", "vocal", "vowel", "size", "resonance", "excite", "mix", "output" });

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
}

TEST_CASE ("Mode is a choice of the engine's two labels, which no host may automate")
{
    Instance a;
    auto* mode = dynamic_cast<juce::AudioParameterChoice*> (&a.p.parameter (param::mode));
    REQUIRE (mode != nullptr);
    CHECK (mode->choices == juce::StringArray { "Time", "Spectral" });
    CHECK_FALSE (mode->isAutomatable());
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
         * late as the host was told. */
        std::vector<float> out;
        for (int b = 0; b < 4; ++b)
        {
            a.run ([b] (int, int i) { return b == 0 && i == 0 ? 1.0f : 0.0f; });
            out.insert (out.end(), a.buffer.getReadPointer (0), a.buffer.getReadPointer (0) + block);
        }
        const auto peak = std::max_element (out.begin(), out.end(), [] (float x, float y) { return std::abs (x) < std::abs (y); });
        CHECK (peak - out.begin() == latency);
        CHECK (*peak == 1.0f);
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
        double want[VZ_FORMANTS] {};
        vz_formants (v, 30.0, 65.0, want);
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
    CHECK_FALSE (a.p.bipolar (99));
}

TEST_CASE ("the model: the picture waits for audio, then says which engine drew it")
{
    Instance a;
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
