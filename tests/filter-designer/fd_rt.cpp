// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Filter Designer's audio callback allocates nothing, on the JUCE shell.
 *
 *   fd_rt      (run with alloc_guard inserted)
 *
 * The processor itself, as its VST3 wrapper drives it, under alloc_guard.c:
 * every malloc the calling thread makes inside processBlock is counted -- the
 * shell's C++, JUCE's, and the Rust engine's alike. The blocks cover what a
 * session does to it: playback with the window open (the analyzer's rings
 * published), every control automated from either side -- every type,
 * family, order and channel switching, so every fade and every design runs
 * -- a reactivation, another rate, the host's bypass, a mono track, a block
 * longer than the host announced and a set loaded. Each must count zero.
 *
 * The guard is shown an allocation first, so a guard that was not inserted
 * fails rather than passes. macOS only: the guard is a dyld interposer.
 */
#include "FilterDesigner.h"

#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
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

std::vector<float>* volatile escape = nullptr;

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;
} // namespace

int main()
{
    const juce::ScopedJuceInitialiser_GUI gui;
    std::printf ("fd_rt\n");

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

    ni::fd::Processor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::AudioBuffer<float> longer (2, 2 * blockSize);
    juce::MidiBuffer none;
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    juce::int64 sample = 0;

    long allocations = 0, writes = 0;
    const auto play = [&] (int blocks, bool bypassed = false, juce::AudioBuffer<float>* b = nullptr,
                           const std::function<void (int)>& each = {}, const std::function<void (int)>& inside = {})
    {
        auto& use = b != nullptr ? *b : buffer;
        for (int k = 0; k < blocks; ++k)
        {
            for (int ch = 0; ch < use.getNumChannels(); ++ch)
                for (int i = 0; i < use.getNumSamples(); ++i)
                    use.setSample (ch, i, 0.5f * (float) std::sin (0.031 * (double) (sample + i) + ch));
            if (each)
                each (k);
            if (inside)
            {
                begin();
                inside (k);
                writes += end();
            }
            begin();
            if (bypassed)
                processor.processBlockBypassed (use, none);
            else
                processor.processBlock (use, none);
            allocations += end();
            sample += use.getNumSamples();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
    };

    play (100);
    check (allocations == 0, "two seconds of playback, empty", allocations);

    allocations = 0;
    processor.editorOpened();
    play (100);
    check (allocations == 0, "playback with the window open: the analyzer's rings", allocations);

    /* Every control moved while it plays, from the message thread's side. */
    const int n = ni::fd::Processor::paramCount;
    allocations = 0;
    play (n * 3, false, nullptr, [&] (int k) {
        auto& p = processor.parameter (k % n);
        p.setValueNotifyingHost ((float) ((k / n + k) % 5) / 4.0f);
    });
    check (allocations == 0, "every control automated while it plays", allocations);

    /* And from the audio thread's side, as a host's automation arrives. */
    allocations = 0;
    writes = 0;
    play (n * 3, false, nullptr, {}, [&] (int k) {
        auto& p = processor.parameter (k % n);
        p.setValueNotifyingHost ((float) ((k / n + k + 2) % 5) / 4.0f);
    });
    check (allocations == 0, "every control automated from the audio thread: the blocks", allocations);
    check (writes == 0, "... and the writes themselves", writes);

    allocations = 0;
    processor.prepareToPlay (sampleRate, blockSize);
    play (20);
    check (allocations == 0, "a reactivation: the rate and the reset applied", allocations);

    allocations = 0;
    processor.prepareToPlay (96000.0, blockSize);
    play (20);
    check (allocations == 0, "a reactivation at another rate: every design made again", allocations);
    processor.prepareToPlay (sampleRate, blockSize);

    allocations = 0;
    for (int k = 0; k < 20; ++k)
        play (1, k % 2 == 0);
    check (allocations == 0, "bypassed and back, again and again", allocations);

    allocations = 0;
    play (4, false, &longer);
    check (allocations == 0, "a block longer than the host announced", allocations);

    allocations = 0;
    processor.setStateInformation (saved.getData(), (int) saved.getSize());
    play (20);
    check (allocations == 0, "a set loaded while playing", allocations);

    processor.editorClosed();
    processor.releaseResources();

    {
        ni::fd::Processor mono;
        auto layout = mono.getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (mono.setBusesLayout (layout), "a mono layout is taken", 0);
        mono.prepareToPlay (sampleRate, blockSize);
        mono.parameter (ni::fd::param::node (0, ni::fd::param::type)).setValueNotifyingHost (0.5f);
        juce::AudioBuffer<float> one (1, blockSize);
        long monoAllocations = 0;
        for (int k = 0; k < 60; ++k)
        {
            for (int i = 0; i < blockSize; ++i)
                one.setSample (0, i, 0.5f * (float) std::sin (0.02 * (double) (k * blockSize + i)));
            begin();
            mono.processBlock (one, none);
            monoAllocations += end();
        }
        check (monoAllocations == 0, "a mono track", monoAllocations);
    }

    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
