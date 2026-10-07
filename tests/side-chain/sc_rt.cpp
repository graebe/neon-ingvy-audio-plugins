// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain's audio callback allocates nothing, on the JUCE shell.
 *
 *   sc_rt <tests/fixtures/iplug2>      (run with alloc_guard inserted)
 *
 * The processor itself, as its VST3 wrapper drives it, under alloc_guard.c:
 * every malloc the calling thread makes inside processBlock is counted -- the
 * shell's C++, JUCE's, and the Rust engine's alike. The blocks cover what a
 * session does to it: a set loaded, playback under a running transport with
 * the window open (the scope and the Ground at work), a key ducking on
 * Sidechain, MIDI notes and a panic, the host's bypass with MIDI in it, a
 * block longer than the host announced, and a second load. Each must count
 * zero.
 *
 * The guard is shown an allocation first, so a guard that was not inserted
 * -- and would count nothing ever -- fails rather than passes. macOS only:
 * the guard is a dyld interposer.
 */
#include "EngineModel.h"
#include "SideChain.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what, long count)
{
    std::printf ("  %-64s %s (%ld)\n", what.toRawUTF8(), ok ? "ok" : "FAIL", count);
    if (! ok)
        ++failures;
}

/* Where the guard's own check lets an allocation be seen. */
std::vector<float>* volatile escape = nullptr;

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

struct Transport final : juce::AudioPlayHead
{
    juce::int64 sample = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true);
        p.setBpm (120.0);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setTimeInSamples (sample);
        p.setPpqPosition ((double) sample / sampleRate * 2.0);
        return p;
    }
};
} // namespace

int main (int argc, char* argv[])
{
    if (argc != 2)
    {
        std::fprintf (stderr, "usage: sc_rt <fixtures-dir>\n");
        return 2;
    }
    const juce::ScopedJuceInitialiser_GUI gui;
    const auto fixtures = juce::File (argv[1]).getChildFile ("NISideChain");
    std::printf ("sc_rt\n");

    const auto begin = reinterpret_cast<void (*)()> (dlsym (RTLD_DEFAULT, "ni_alloc_watch_begin"));
    const auto end = reinterpret_cast<long (*)()> (dlsym (RTLD_DEFAULT, "ni_alloc_watch_end"));
    if (begin == nullptr || end == nullptr)
    {
        std::printf ("  the allocation guard is not inserted (DYLD_INSERT_LIBRARIES)  FAIL\n");
        return 1;
    }
    begin();
    void* volatile shown = std::malloc (64);
    std::free (shown);
    check (end() >= 1, "the guard counts a malloc it is shown", 1);
    begin();
    {
        std::vector<float> grown (1000);
        escape = &grown;
    }
    escape = nullptr;
    check (end() >= 1, "... and a std::vector's, through the system's operator new", 1);

    ni::sc::Processor processor;
    auto layout = processor.getBusesLayout();
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    if (! processor.setBusesLayout (layout))
    {
        std::printf ("  the key bus connects  FAIL\n");
        return 1;
    }
    Transport transport;
    processor.setPlayHead (&transport);
    processor.prepareToPlay (sampleRate, blockSize);

    /* Main and key, and room for a block twice as long as announced. Made
     * here, before any block is watched. */
    juce::AudioBuffer<float> buffer (4, blockSize);
    juce::AudioBuffer<float> longer (4, 2 * blockSize);
    juce::MidiBuffer none, notes, panic;
    notes.ensureSize (256);
    notes.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 17);
    notes.addEvent (juce::MidiMessage::noteOff (1, 36), 400);
    panic.addEvent (juce::MidiMessage::controllerEvent (1, 123, 0), 0);
    panic.addEvent (juce::MidiMessage::controllerEvent (1, 120, 0), 1);

    long allocations = 0;
    const auto play = [&] (int blocks, juce::MidiBuffer& messages, bool bypassed = false,
                           juce::AudioBuffer<float>* b = nullptr)
    {
        auto& use = b != nullptr ? *b : buffer;
        for (int k = 0; k < blocks; ++k)
        {
            for (int ch = 0; ch < 4; ++ch)
                for (int i = 0; i < use.getNumSamples(); ++i)
                    use.setSample (ch, i, ch < 2 ? 0.5f : ((transport.sample + i) % 9600 < 480 ? 0.8f : 0.0f));
            begin();
            if (bypassed)
                processor.processBlockBypassed (use, messages);
            else
                processor.processBlock (use, messages);
            allocations += end();
            transport.sample += use.getNumSamples();
        }
    };
    const auto load = [&] (const char* scenario)
    {
        juce::MemoryBlock state;
        fixtures.getChildFile (juce::String (scenario) + ".component.bin").loadFileAsData (state);
        processor.setStateInformation (state.getData(), (int) state.getSize());
    };

    load ("default");
    play (200, none);
    check (allocations == 0, "a loaded set, then two seconds of Cycle", allocations);

    allocations = 0;
    processor.editorOpened();
    play (100, none);
    check (allocations == 0, "playback with the window open: the scope and the Ground", allocations);

    allocations = 0;
    load ("custom");
    play (60, none);
    check (allocations == 0, "a key ducking on Sidechain", allocations);

    allocations = 0;
    processor.parameter (ni::sc::kSource).setPlainNotifyingHost (1.0);
    play (20, notes);
    play (2, panic);
    check (allocations == 0, "MIDI notes and a panic", allocations);

    allocations = 0;
    play (20, notes, true);
    check (allocations == 0, "the host's bypass, MIDI in it", allocations);

    allocations = 0;
    play (4, notes, false, &longer);
    check (allocations == 0, "a block longer than the host announced", allocations);

    allocations = 0;
    load ("default");
    play (20, none);
    check (allocations == 0, "a second set loaded while playing", allocations);

    processor.editorClosed();
    processor.releaseResources();
    processor.setPlayHead (nullptr);
    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
