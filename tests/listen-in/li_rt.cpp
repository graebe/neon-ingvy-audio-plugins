// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In's audio callback allocates nothing, on the JUCE shell.
 *
 *   li_rt <tests/fixtures/iplug2>      (run with alloc_guard inserted)
 *
 * The processor itself, as its VST3 wrapper drives it, under alloc_guard.c:
 * every malloc the calling thread makes inside processBlock is counted -- the
 * shell's C++, JUCE's, and the bus's Rust alike. The blocks cover what a
 * session does to it: playback before any bus is claimed, publishing on a
 * claimed bus, a block longer than the stage, the window open (the Ground at
 * work), the host moving Bus (the pusher handed over between blocks), a set
 * loaded with its name, the host's bypass, and a block on a bus another
 * instance holds. Each must count zero.
 *
 * The guard is shown an allocation first, so a guard that was not inserted
 * -- and would count nothing ever -- fails rather than passes. macOS only:
 * the guard is a dyld interposer.
 */
#include "ListenIn.h"

#include <dlfcn.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
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
        std::fprintf (stderr, "usage: li_rt <fixtures-dir>\n");
        return 2;
    }
    /* A bus namespace of this process's own (li_processor.cpp says why). */
    const auto ns = "li_rt." + std::to_string ((long) getpid());
    setenv ("NIA_BUS_NS", ns.c_str(), 1);

    const juce::ScopedJuceInitialiser_GUI gui;
    const auto fixtures = juce::File (argv[1]).getChildFile ("NIListenIn");
    std::printf ("li_rt\n");

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

    ni::li::Processor processor;
    Transport transport;
    processor.setPlayHead (&transport);
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, ni::li::Processor::stageFrames * 2);
    juce::MidiBuffer midi;
    long allocations = 0;
    const auto play = [&] (int blocks, int frames = blockSize)
    {
        buffer.setSize (2, frames, false, false, true);
        for (int b = 0; b < blocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < frames; ++i)
                    buffer.setSample (ch, i, 0.5f);
            begin();
            processor.processBlock (buffer, midi);
            allocations += end();
            transport.sample += frames;
        }
    };

    play (100);
    check (allocations == 0, "playback before the bus is claimed", allocations);

    allocations = 0;
    processor.serviceBus();
    play (200);
    check (processor.status() == ni::li::Status::live, "the bus is claimed", 0);
    check (allocations == 0, "two seconds published on the bus", allocations);

    allocations = 0;
    play (10, ni::li::Processor::stageFrames + 1000);
    check (allocations == 0, "blocks longer than the stage", allocations);

    allocations = 0;
    processor.editorOpened();
    play (100);
    check (allocations == 0, "playback with the window open: the Ground", allocations);

    allocations = 0;
    processor.busParameter().setPlainNotifyingHost (5.0);
    play (5);
    processor.serviceBus();
    play (20);
    processor.serviceBus();
    play (20);
    check (allocations == 0, "the host moves Bus: the pusher handed over", allocations);

    allocations = 0;
    juce::MemoryBlock state;
    fixtures.getChildFile ("labelled.component.bin").loadFileAsData (state);
    processor.setStateInformation (state.getData(), (int) state.getSize());
    play (5);
    processor.serviceBus();
    play (20);
    check (allocations == 0, "a set loaded while playing, and its bus claimed", allocations);

    allocations = 0;
    processor.getBypassParameter()->setValueNotifyingHost (1.0f);
    play (20);
    processor.getBypassParameter()->setValueNotifyingHost (0.0f);
    check (allocations == 0, "the host's bypass", allocations);

    allocations = 0;
    {
        ni::li::Processor second;
        second.prepareToPlay (sampleRate, blockSize);
        second.busParameter().setPlainNotifyingHost (processor.busParameter().plain());
        second.serviceBus();
        check (second.status() == ni::li::Status::taken, "a second instance on the same bus is refused", 0);
        buffer.setSize (2, blockSize, false, false, true);
        buffer.clear();
        begin();
        second.processBlock (buffer, midi);
        allocations += end();
    }
    check (allocations == 0, "a block on a bus another instance holds", allocations);

    processor.editorClosed();
    processor.releaseResources();
    processor.setPlayHead (nullptr);
    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
