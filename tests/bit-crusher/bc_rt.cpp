// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bit-Crusher's audio callback allocates nothing, on the JUCE shell.
 *
 *   bc_rt      (run with alloc_guard inserted)
 *
 * The processor itself, as its VST3 wrapper drives it, under alloc_guard.c:
 * every malloc the calling thread makes inside processBlock is counted -- the
 * shell's C++, JUCE's, and the Rust engine's alike. The blocks cover what a
 * session does to it: playback with the window open (the scope at work),
 * every control automated while it plays -- each glide, retune and crossfade
 * -- a reactivation's reset, either bypass, a mono track, a block longer than
 * the host announced, and a set loaded. Each must count zero.
 *
 * The guard is shown an allocation first, so a guard that was not inserted
 * -- and would count nothing ever -- fails rather than passes. macOS only:
 * the guard is a dyld interposer.
 */
#include "BitCrusher.h"

#include <dlfcn.h>

#include <cmath>
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

int main()
{
    const juce::ScopedJuceInitialiser_GUI gui;
    std::printf ("bc_rt\n");

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

    ni::bc::Processor processor;
    Transport transport;
    processor.setPlayHead (&transport);
    processor.prepareToPlay (sampleRate, blockSize);

    /* Made here, before any block is watched. */
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::AudioBuffer<float> longer (2, 2 * blockSize);
    juce::MidiBuffer none;
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);

    long allocations = 0;
    const auto play = [&] (int blocks, bool bypassed = false, juce::AudioBuffer<float>* b = nullptr,
                           const std::function<void (int)>& each = {})
    {
        auto& use = b != nullptr ? *b : buffer;
        for (int k = 0; k < blocks; ++k)
        {
            for (int ch = 0; ch < use.getNumChannels(); ++ch)
                for (int i = 0; i < use.getNumSamples(); ++i)
                    use.setSample (ch, i, 0.7f * (float) std::sin (0.031 * (double) (transport.sample + i)));
            if (each)
                each (k);
            begin();
            if (bypassed)
                processor.processBlockBypassed (use, none);
            else
                processor.processBlock (use, none);
            allocations += end();
            transport.sample += use.getNumSamples();
        }
    };

    play (100);
    check (allocations == 0, "two seconds of playback", allocations);

    allocations = 0;
    processor.editorOpened();
    play (100);
    check (allocations == 0, "playback with the window open: the scope", allocations);

    /* Every control moved while it plays, from the message thread's side: a
     * new value each block, so a glide, a retune or a crossfade is always
     * running. */
    allocations = 0;
    play (ni::bc::Processor::paramCount * 8, false, nullptr, [&] (int k) {
        auto& p = processor.parameter (k % ni::bc::Processor::paramCount);
        p.setValueNotifyingHost ((float) ((k / ni::bc::Processor::paramCount) % 5) / 4.0f);
    });
    check (allocations == 0, "every control automated while it plays", allocations);

    allocations = 0;
    processor.prepareToPlay (sampleRate, blockSize);
    play (20);
    check (allocations == 0, "a reactivation: the rate and the reset applied", allocations);

    allocations = 0;
    play (20, true);
    check (allocations == 0, "the host's bypass", allocations);

    allocations = 0;
    play (4, false, &longer);
    check (allocations == 0, "a block longer than the host announced", allocations);

    allocations = 0;
    processor.setStateInformation (saved.getData(), (int) saved.getSize());
    play (20);
    check (allocations == 0, "a set loaded while playing", allocations);

    processor.editorClosed();
    processor.releaseResources();
    processor.setPlayHead (nullptr);
    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
