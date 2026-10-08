// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Spectrogram on the JUCE shell, its processor in the program: the one
 * parameter a host sees, the session a set keeps, the receiver's life, the
 * editor's model on the real analyzer, a real Listen-In bus, and the native
 * editor driven by that model.
 *
 *   the parameter   Bypass alone, as the iPlug2 build reported it
 *                   (parameters.json), at index -- and VST3 ID -- 0
 *   the fixtures    both tests/fixtures/iplug2/NISpectrogram states load to
 *                   their session and bypass and save back byte for byte;
 *                   what no build wrote changes nothing; a load on the host's
 *                   thread is what a save straight after it writes
 *   the audio       through bit for bit, bypassed or not
 *   the receiver    rebuilt on the message thread for the host's rate, which
 *                   reaches the axis; its columns dropped while no window is
 *                   open
 *   the model       a tone drawn at its frequency, from the engine straight
 *                   into the editor's buffers; the host's clock; the
 *                   session's commands, applied at once, each marking the set
 *                   unsaved as no load does; the Ground
 *   the bus         a Listen-In's bus, from a sender in another process: its
 *                   name listed, its tone drawn, and the clash measured
 *                   between it and this track
 *   the editor      on the real model: live, drawing, its controls following
 *                   a session loaded on another thread while it is open
 *
 * What is the editor's own behaviour against a model that does as told is
 * tests/ui's (spectrogram_*.cpp, on a fake); this is what only the real model
 * can show. sg_host.cpp holds the built VST3, as a DAW hosts it.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "EngineModel.h"
#include "PluginEditor.h"
#include "SpectrogramEditor.h"
#include "SpectrogramProcessor.h"
#include "State.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if ! JUCE_WINDOWS
 #include <signal.h>
 #include <spawn.h>
 #include <sys/wait.h>
 #include <unistd.h>
extern char** environ;
#endif

using namespace ni::spectrogram;
using Bytes = std::vector<std::uint8_t>;
namespace st = spectro::state;

namespace
{
const juce::File fixtures = juce::File (NI_FIXTURES).getChildFile ("NISpectrogram");

Bytes fixture (const juce::String& scenario)
{
    juce::MemoryBlock b;
    REQUIRE (fixtures.getChildFile (scenario + ".component.bin").loadFileAsData (b));
    const auto* p = static_cast<const std::uint8_t*> (b.getData());
    return Bytes (p, p + b.getSize());
}

/* A transport playing at 120 BPM in 3/4 from bar 1, moved on by the caller. */
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
        p.setTimeSignature (TimeSignature { 3, 4 });
        p.setTimeInSamples (sample);
        p.setPpqPosition ((double) sample / rate * 2.0);
        return p;
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

/* One instance, as a host runs it: prepared, and its receiver built for the
 * host's rate by a first service, as its timer would. */
struct Instance
{
    static constexpr int frames = 512;

    Processor p;
    Playhead head;
    HostEar ear;
    double phase = 0.0;

    explicit Instance (double rate = 48000.0)
    {
        head.rate = rate;
        p.prepareToPlay (rate, frames);
        p.service();
        p.addListener (&ear);
    }
    ~Instance() { p.removeListener (&ear); }

    EngineModel& model() { return p.model(); }

    /* Blocks of a sine at `hz` (silence for 0), under the transport. */
    void play (int blocks, double hz = 0.0)
    {
        p.setPlayHead (&head);
        juce::AudioBuffer<float> buffer (2, frames);
        juce::MidiBuffer midi;
        const double step = juce::MathConstants<double>::twoPi * hz / head.rate;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < frames; ++i)
            {
                const auto v = hz > 0.0 ? (float) (0.5 * std::sin (phase)) : 0.0f;
                phase += step;
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            p.processBlock (buffer, midi);
            head.sample += frames;
        }
        p.setPlayHead (nullptr);
    }

    Bytes save()
    {
        juce::MemoryBlock m;
        p.getStateInformation (m);
        const auto* b = static_cast<const std::uint8_t*> (m.getData());
        return Bytes (b, b + m.getSize());
    }

    void load (const Bytes& b) { p.setStateInformation (b.data(), (int) b.size()); }

    bool bypassed() { return p.getBypassParameter()->getValue() >= 0.5f; }
};

/* One take of the model's columns. */
struct Take
{
    std::vector<std::uint8_t> levels = std::vector<std::uint8_t> ((std::size_t) (Model::maxColumns * 256));
    std::vector<std::uint8_t> clash = std::vector<std::uint8_t> ((std::size_t) (Model::maxColumns * 256));
    int columns = 0;
    int clashColumns = 0;

    int take (Model& m)
    {
        columns = m.takeColumns (levels.data(), clash.data(), Model::maxColumns, clashColumns);
        return columns;
    }

    /* The loudest band of the newest column. */
    int loudest (int bands) const
    {
        const auto* c = levels.data() + (std::size_t) ((columns - 1) * bands);
        return (int) (std::max_element (c, c + bands) - c);
    }

    /* The most the clash lit in any cell of this take. */
    int clashPeak (int bands) const
    {
        const auto end = clash.begin() + (long) (clashColumns * bands);
        return clashColumns > 0 ? (int) *std::max_element (clash.begin(), end) : 0;
    }
};

/*
 * Plays `hz` and services the receiver, as the host's audio and the timer
 * would, until `until` holds for a take or `seconds` pass. The analysis runs
 * on its own thread, so this waits for it rather than for a count.
 */
template <class Until>
bool runUntil (Instance& a, Take& t, double hz, double seconds, Until until)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double> (seconds);
    while (std::chrono::steady_clock::now() < end)
    {
        a.play (2, hz);
        a.p.service();
        if (t.take (a.model()) > 0 && until (t))
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    return false;
}

/* The centre of a band, in Hz. */
float centreOf (Instance& a, int band)
{
    return a.model().bandCentres()[(std::size_t) band];
}

/* Whether a band's centre is within a semitone of `hz`. */
bool near (float centre, double hz)
{
    return std::abs (std::log2 (centre / hz)) < 1.0 / 12.0;
}
} // namespace

/* ------------------------------------------------------------- parameter -- */

TEST_CASE ("Bypass alone, as the iPlug2 build reported it, at index 0")
{
    Instance a;
    const auto doc = juce::JSON::parse (fixtures.getChildFile ("parameters.json"));
    const auto defaults = juce::JSON::parse (fixtures.getChildFile ("default.json"));
    REQUIRE (doc["parameters"].size() == 1);
    const auto& row = doc["parameters"][0];
    CHECK ((int) row["id"] == 65536);

    REQUIRE (a.p.getParameters().size() == 1);
    auto* bypass = a.p.getBypassParameter();
    CHECK (bypass == a.p.getParameters()[0]);
    CHECK (bypass->getName (128) == row["title"].toString());
    CHECK (bypass->getLabel() == row["units"].toString());
    CHECK (bypass->isBoolean());
    CHECK (bypass->getNumSteps() - 1 == (int) row["stepCount"]);
    CHECK (bypass->getDefaultValue() == (float) (double) row["defaultNormalizedValue"]);
    CHECK (bypass->getText (bypass->getDefaultValue(), 128) == defaults["values"][0]["display"].toString());
    CHECK (a.model().numParameters() == 0);
}

TEST_CASE ("stereo in and stereo out, and nothing else: the iPlug2 build's 2-2")
{
    Instance a;
    juce::AudioProcessor::BusesLayout stereo, mono;
    stereo.inputBuses.add (juce::AudioChannelSet::stereo());
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    mono.inputBuses.add (juce::AudioChannelSet::mono());
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    CHECK (a.p.checkBusesLayoutSupported (stereo));
    CHECK_FALSE (a.p.checkBusesLayoutSupported (mono));
}

/* ------------------------------------------------------------- fixtures -- */

TEST_CASE ("both iPlug2 fixtures load to their session and bypass, and save back byte for byte")
{
    for (const char* scenario : { "default", "session" })
    {
        CAPTURE (scenario);
        const auto bytes = fixture (scenario);
        const auto decoded = st::read (bytes.data(), bytes.size(), st::Fields {});
        REQUIRE (decoded.has_value());

        Instance a;
        a.load (bytes);
        CHECK (a.p.session().get() == decoded->fields);
        CHECK (a.bypassed() == decoded->bypass.value_or (false));
        /* The editor sees it at once, before the receiver has it. */
        const auto& s = a.model().session();
        CHECK (s.view == decoded->fields.view);
        CHECK (s.compareA == decoded->fields.cmpA);
        CHECK (s.compareB == decoded->fields.cmpB);
        CHECK (s.clash == decoded->fields.clashOn);
        CHECK (s.rangeLo == decoded->fields.rangeLo);
        CHECK (s.rangeHi == decoded->fields.rangeHi);
        CHECK (s.listen == std::vector<int> (decoded->fields.sources.begin(), decoded->fields.sources.end()));

        a.p.service();
        CHECK (a.p.session().applied() == decoded->fields);
        CHECK (a.save() == bytes);
    }
}

TEST_CASE ("a set saved bypassed reopens bypassed, and saves so again")
{
    st::Fields f;
    f.sources = { 3 };
    f.view = { 0, 1 };
    f.rangeLo = 200.0f;
    f.rangeHi = 4000.0f;
    const auto bytes = st::write (f, true);
    Instance a;
    a.load (bytes);
    CHECK (a.bypassed());
    CHECK (a.save() == bytes);
}

TEST_CASE ("what no build wrote is refused, and changes nothing")
{
    Instance a;
    const auto session = fixture ("session");
    a.load (session);
    std::mt19937 rng (11);
    Bytes noise (4096);
    for (auto& b : noise)
        b = (std::uint8_t) rng();
    a.load (noise);
    a.load ({});
    CHECK (a.save() == session);
}

TEST_CASE ("a load on the host's thread is what a save straight after it writes, and marks nothing unsaved")
{
    Instance a;
    const auto session = fixture ("session");
    std::thread host ([&] { a.load (session); });
    host.join();
    /* Before the message thread has applied it. */
    CHECK (a.p.session().applied() == st::Fields {});
    CHECK (a.save() == session);
    a.p.service();
    CHECK (a.p.session().applied().sources == std::vector<unsigned int> { 2, 5 });
    CHECK (a.ear.dirty == 0);
}

/* ---------------------------------------------------------------- audio -- */

TEST_CASE ("the audio passes through bit for bit, and so does the host's bypass")
{
    Instance a;
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> any (-1.0f, 1.0f);
    juce::AudioBuffer<float> buffer (2, Instance::frames), copy (2, Instance::frames);
    juce::MidiBuffer midi;
    for (const bool bypassed : { false, true })
    {
        CAPTURE (bypassed);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < Instance::frames; ++i)
                buffer.setSample (ch, i, any (rng));
        copy.makeCopyOf (buffer);
        if (bypassed)
            a.p.processBlockBypassed (buffer, midi);
        else
            a.p.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            CHECK (std::memcmp (buffer.getReadPointer (ch), copy.getReadPointer (ch),
                                sizeof (float) * (std::size_t) Instance::frames)
                   == 0);
    }
}

/* ------------------------------------------------------------- receiver -- */

TEST_CASE ("the host's rate reaches the axis: a receiver for it is built on the message thread")
{
    Instance a (48000.0);
    REQUIRE (a.model().bandCentres().size() == 256);
    CHECK (a.model().bandCentres().front() == doctest::Approx (10.0f).epsilon (0.1));
    CHECK (a.model().bandCentres().back() > 18000.0f);

    /* A new rate: nothing is fed until the receiver is rebuilt for it. */
    a.p.prepareToPlay (32000.0, Instance::frames);
    CHECK (a.p.rebuildPending());
    Take t;
    CHECK (t.take (a.model()) == 0);
    a.play (4);
    a.p.service();
    CHECK_FALSE (a.p.rebuildPending());
    /* Nothing above half the rate exists to draw. */
    CHECK (a.model().bandCentres().back() <= 16000.0f);
    CHECK (a.model().transport().sampleRate == 32000);
}

TEST_CASE ("with no window open the columns are dropped, so an opened window draws only what is new")
{
    Instance a;
    for (int i = 0; i < 40; ++i)
    {
        a.play (2, 440.0);
        a.p.service();
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    /* The analysis catches up and the service drains it. */
    for (int i = 0; i < 30; ++i)
    {
        a.p.service();
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    a.p.editorOpened();
    Take t;
    CHECK (t.take (a.model()) == 0);
    a.p.editorClosed();
}

/* ---------------------------------------------------------------- model -- */

TEST_CASE ("a tone is drawn at its frequency, from the analyzer straight into the editor's buffers")
{
    Instance a;
    a.p.editorOpened();
    Take t;
    const bool drawn = runUntil (a, t, 1000.0, 5.0, [&] (const Take& k) {
        return k.levels[(std::size_t) ((k.columns - 1) * 256 + k.loudest (256))] > 128;
    });
    REQUIRE (drawn);
    CHECK (t.columns <= Model::maxColumns);
    CHECK (near (centreOf (a, t.loudest (256)), 1000.0));
    /* No clash asked for, none handed over. */
    CHECK (t.clashColumns == 0);
    a.p.editorClosed();
}

TEST_CASE ("the host's clock: its position while it runs, the last tempo's while it stops")
{
    Instance a;
    a.head.sample = 48000 * 3;
    a.play (10);
    auto t = a.model().transport();
    CHECK (t.running);
    CHECK (t.bpm == 120.0);
    CHECK (t.numerator == 3);
    CHECK (t.denominator == 4);
    CHECK (t.sampleRate == 48000);
    const double lastBlock = (double) (a.head.sample - Instance::frames) / 48000.0 * 2.0;
    CHECK (t.ppq == doctest::Approx (lastBlock));
    const int hop = spectro_pick_hop (48000.0f, spectro_pick_fft_size (48000.0f));
    CHECK (t.ppqPerColumn == doctest::Approx ((double) hop / 48000.0 * 2.0));

    /* Stopped: the picture keeps filling, at the last tempo. */
    a.head.playing = false;
    a.play (10);
    const auto stopped = a.model().transport();
    CHECK_FALSE (stopped.running);
    CHECK (stopped.ppq == doctest::Approx (t.ppq + 10.0 * Instance::frames / 48000.0 * 2.0));
}

TEST_CASE ("the session's commands apply at once and mark the set unsaved; a load does not")
{
    Instance a;
    auto& m = a.model();

    m.setRange (40.0f, 800.0f);
    CHECK (m.session().rangeLo == 40.0f);
    CHECK (m.session().rangeHi == 800.0f);
    /* The axis the engine made of it, at once. */
    CHECK (m.bandCentres().front() == doctest::Approx (40.0f).epsilon (0.1));
    CHECK (m.bandCentres().back() == doctest::Approx (800.0f).epsilon (0.1));
    CHECK (a.ear.dirty == 1);

    m.setLook ({ 0, 1 }, 0, 1, true, { 3, 0, -2 });
    CHECK (m.session().view == std::vector<int> { 0, 1 });
    CHECK (m.session().clash);
    CHECK (m.session().listen == std::vector<int> { 3 });
    CHECK (a.p.session().applied().sources == std::vector<unsigned int> { 3 });
    CHECK (a.ear.dirty == 2);

    /* An empty view is the own channel: a spectrogram showing nothing is a
     * broken plugin, not a view. */
    m.setLook ({}, 0, 1, false, {});
    CHECK (m.session().view == std::vector<int> { 0 });
    CHECK (a.ear.dirty == 3);

    m.setClashCriteria (-50.0f, 10.0f);
    CHECK (m.session().clashFloorDb == -50.0f);
    CHECK (a.p.session().applied().clashBalanceDb == 10.0f);
    CHECK (a.ear.dirty == 4);

    /* What a set writes now is what the window shows. */
    const auto saved = a.save();
    const auto back = st::read (saved.data(), saved.size(), st::Fields {});
    REQUIRE (back.has_value());
    CHECK (back->fields.rangeLo == 40.0f);
    CHECK (back->fields.clashFloorDb == -50.0f);

    a.load (fixture ("default"));
    a.p.service();
    CHECK (a.ear.dirty == 4);
    CHECK (m.session().rangeLo == 10.0f);
    CHECK (m.bandCentres().back() > 18000.0f);
}

TEST_CASE ("the Ground rings with the host's beat, only while a window is open")
{
    Instance a;
    float rings[16];
    a.play (188); /* two seconds, four beats, with no window */
    a.p.editorOpened();
    CHECK (a.model().takeRings (rings, 16) == 0);
    a.play (94); /* a second: two beats */
    const int n = a.model().takeRings (rings, 16);
    CHECK (n >= 1);
    CHECK (n <= 3);
    a.p.editorClosed();
}

/* ------------------------------------------------------------------ bus -- */

#if ! JUCE_WINDOWS
namespace
{
/* A Listen-In's sending half in a process of its own (sg_bus_sender.c), in
 * this program's bus namespace; stopped and waited for when it goes. */
struct Sender
{
    pid_t pid = -1;

    Sender (int slot, double hz, const char* label)
    {
        const auto s = std::to_string (slot), f = std::to_string (hz);
        char* argv[] = { (char*) NI_BUS_SENDER, (char*) s.c_str(), (char*) f.c_str(), (char*) "30",
                         (char*) label, nullptr };
        if (posix_spawn (&pid, NI_BUS_SENDER, nullptr, nullptr, argv, environ) != 0)
            pid = -1;
    }
    ~Sender()
    {
        if (pid <= 0)
            return;
        kill (pid, SIGTERM);
        int status = 0;
        waitpid (pid, &status, 0);
    }
};
} // namespace

TEST_CASE ("a Listen-In's bus is listed by its name, its tone drawn, and its clash with this track measured")
{
    constexpr int slot = 7;
    Sender sender (slot, 2000.0, "Pad");
    REQUIRE (sender.pid > 0);

    Instance a;
    auto& m = a.model();
    a.p.editorOpened();

    /* The bus appears at human speed: the list is looked at twice a second.
     * The deadline only bounds a hang -- the loop ends the moment the bus is
     * listed, within a second or two on an idle machine. It is generous because the
     * sender is a second process: in a cold build directory ctest starts it
     * beside the cargo suites compiling their tests, and macOS scans a freshly
     * linked binary on its first launch, which once took this past five
     * seconds (the cross build's first ctest run, scripts/build-macos.sh). */
    const auto listed = [&] {
        for (const auto& s : m.sources())
            if (s.slot == slot && s.label == "Pad" && s.live && s.sampleRate == 48000)
                return true;
        return false;
    };
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds (20);
    while (! listed() && std::chrono::steady_clock::now() < until)
    {
        a.play (2);
        a.p.service();
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
    }
    REQUIRE (listed());

    /* The bus alone in the picture: channel 1, the first bus opened. */
    m.setLook ({ 1 }, 0, 1, false, { slot });
    Take t;
    REQUIRE (runUntil (a, t, 0.0, 5.0, [&] (const Take& k) {
        return k.levels[(std::size_t) ((k.columns - 1) * 256 + k.loudest (256))] > 128;
    }));
    CHECK (near (centreOf (a, t.loudest (256)), 2000.0));

    /* This track plays the same note: the two clash, measured for the same
     * columns the picture is drawn from. */
    m.setLook ({ 0, 1 }, 0, 1, true, { slot });
    REQUIRE (runUntil (a, t, 2000.0, 5.0, [&] (const Take& k) { return k.clashPeak (256) > 128; }));
    CHECK (t.clashColumns == t.columns);
    a.p.editorClosed();
}
#endif

/* --------------------------------------------------------------- editor -- */

TEST_CASE ("the editor on the real model: live, drawing the tone, and following a set loaded on another thread")
{
    Instance a;
    std::unique_ptr<juce::AudioProcessorEditor> window (a.p.createEditorAndMakeActive());
    REQUIRE (window != nullptr);
    CHECK (a.p.editorOpen());
    CHECK (window->getWidth() == SpectrogramEditor::designWidth);
    CHECK (window->getHeight() == SpectrogramEditor::designHeight);
    auto* editor = dynamic_cast<SpectrogramEditor*> (&dynamic_cast<ni::PluginEditor&> (*window).getDesign());
    REQUIRE (editor != nullptr);
    auto& view = editor->view();

    double now = 0.0;
    view.update (now);
    CHECK_FALSE (view.isLive());
    for (int i = 0; i < 300 && ! view.isLive(); ++i)
    {
        a.play (2, 1000.0);
        a.p.service();
        view.update (now += 16.0);
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    REQUIRE (view.isLive());
    const auto& picture = view.picture();
    bool lit = false;
    for (int c = 0; c < picture.getColumns() && ! lit; ++c)
        for (int b = 0; b < picture.getBands() && ! lit; ++b)
            lit = picture.levelAt (ni::ui::Spectrogram::View::scroll, c, b) > 128;
    CHECK (lit);

    /* A set loaded by the host on its own thread while the window is open:
     * the controls show it on the next frame, before the receiver has it. */
    const auto session = fixture ("session");
    std::thread host ([&] { a.load (session); });
    host.join();
    view.update (now += 16.0);
    CHECK (view.rangeSelect().getOptions()[view.rangeSelect().getIndex()] == "Bass");
    CHECK (view.viewList().getSelected() == std::vector<int> { 0, 1, 2 });
    CHECK (view.clashSwitch().isOn());
    a.p.service();
    view.update (now += 16.0);
    CHECK (a.model().bandCentres().front() == doctest::Approx (40.0f).epsilon (0.1));

    window.reset();
    CHECK_FALSE (a.p.editorOpen());
}

int main (int argc, char** argv)
{
    /* A bus namespace of this run's own, before anything touches a bus: the
     * slots probed and claimed here are never a Live session's. */
    const auto ns = "sg_processor." + std::to_string ((long long) juce::Time::currentTimeMillis());
#if JUCE_WINDOWS
    _putenv_s ("NIA_BUS_NS", ns.c_str());
#else
    setenv ("NIA_BUS_NS", ns.c_str(), 1);
#endif
    const juce::ScopedJuceInitialiser_GUI gui;
    doctest::Context context (argc, argv);
    return context.run();
}
