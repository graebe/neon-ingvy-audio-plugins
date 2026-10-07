// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate's audio callback allocates nothing, on the JUCE shell.
 *
 *   tg_rt <tests/fixtures/iplug2>      (run with alloc_guard inserted)
 *
 * The processor itself, as its VST3 wrapper drives it, under alloc_guard.c:
 * every malloc the calling thread makes inside processBlock is counted -- the
 * shell's C++, JUCE's, and the Rust engine's alike. The blocks cover what a
 * session does to it: a set loaded (the engine applies it at the top of a
 * block), playback under a running transport with the window open (the scope
 * and the Ground at work), the host moving Slot (the switch, and the follow
 * it publishes), the editor's edits and a paste landing, the host's bypass,
 * and a second load. Each must count zero.
 *
 * The guard is shown an allocation first, so a guard that was not inserted
 * -- and would count nothing ever -- fails rather than passes. macOS only:
 * the guard is a dyld interposer.
 */
#include "EngineModel.h"
#include "TranceGate.h"

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
        std::fprintf (stderr, "usage: tg_rt <fixtures-dir>\n");
        return 2;
    }
    const juce::ScopedJuceInitialiser_GUI gui;
    const auto fixtures = juce::File (argv[1]).getChildFile ("NITranceGate");
    std::printf ("tg_rt\n");

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

    ni::tg::Processor processor;
    Transport transport;
    processor.setPlayHead (&transport);
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    long allocations = 0;
    const auto play = [&] (int blocks, bool bypassed = false)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (ch, i, 0.5f);
            begin();
            if (bypassed)
                processor.processBlockBypassed (buffer, midi);
            else
                processor.processBlock (buffer, midi);
            allocations += end();
            transport.sample += blockSize;
        }
    };
    const auto load = [&] (const char* scenario)
    {
        juce::MemoryBlock state;
        fixtures.getChildFile (juce::String (scenario) + ".component.bin").loadFileAsData (state);
        processor.setStateInformation (state.getData(), (int) state.getSize());
    };

    load ("slots");
    play (200);
    check (allocations == 0, "a loaded set, then two seconds of playback", allocations);

    allocations = 0;
    processor.editorOpened();
    play (100);
    check (allocations == 0, "playback with the window open: the scope and the Ground", allocations);

    allocations = 0;
    processor.parameter (ni::tg::kSlot).setPlainNotifyingHost (3.0);
    play (20);
    processor.followEngine();
    play (20);
    check (allocations == 0, "the host moves Slot: the switch and its follow", allocations);

    allocations = 0;
    auto& model = processor.model();
    model.setStep (3, ni::tg::StepMode::tie);
    model.setDepth (3, 0.4f);
    model.randomize();
    model.paste (model.exportText (true));
    play (20);
    check (allocations == 0, "the editor's edits and a pasted bank land", allocations);

    allocations = 0;
    play (20, true);
    check (allocations == 0, "the host's bypass", allocations);

    allocations = 0;
    load ("default");
    play (20);
    check (allocations == 0, "a second set loaded while playing", allocations);

    processor.editorClosed();
    processor.releaseResources();
    processor.setPlayHead (nullptr);
    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
