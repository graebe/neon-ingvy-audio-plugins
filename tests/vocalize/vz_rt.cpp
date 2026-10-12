// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Vocalize's audio callback allocates nothing, on the JUCE shell.
 *
 *   vz_rt      (run with alloc_guard inserted)
 *
 * The processor itself, as its VST3 wrapper drives it, under alloc_guard.c:
 * every malloc the calling thread makes inside processBlock is counted -- the
 * shell's C++, JUCE's, and the Rust engine's alike. The blocks cover what a
 * session does to it: playback in either engine with the window open (the
 * picture and the Ground at work), every control automated while it plays,
 * a reactivation's reset, the host's bypass and the resume after it, the
 * ceiling switched, a mono track, a block longer than the host announced, a
 * set loaded, and a captured voice arriving, morphed by Voice, restored
 * with a set and forgotten. Each must count zero.
 *
 * MODE FROM THE AUDIO THREAD. A VST3 host's parameter changes arrive inside
 * process(), and JUCE's wrapper applies them there (setValueNotifyingHost),
 * so whatever a parameter's write sets off runs on the audio thread. Those
 * writes are made on the watched thread here, as the wrapper makes them, and
 * counted apart from the blocks they precede, so a failure says which of the
 * two allocated; the message loop is let run between blocks, as it runs
 * beside a host's audio thread, where the latency follows Mode (a timer). A
 * switch of engines mid-stream -- its dip, the other engine primed -- and the
 * write that asks for it must each count zero.
 *
 * A VOICE ARRIVES BY THE SHELL'S QUEUE. A capture reads and analyses its
 * recording on a thread of its own, which allocates as it likes and is not
 * watched; its answer reaches the engine as a command applied at the top of a
 * block, by value -- and so does a voice restored from a set, or forgotten.
 *
 * The guard is shown an allocation first, so a guard that was not inserted
 * -- and would count nothing ever -- fails rather than passes. macOS only:
 * the guard is a dyld interposer.
 */
#include "Vocalize.h"
#include "vz_voices.h"

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
    std::printf ("vz_rt\n");

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

    ni::vz::Processor processor;
    Transport transport;
    processor.setPlayHead (&transport);
    processor.prepareToPlay (sampleRate, blockSize);
    auto& mode = processor.parameter (ni::vz::param::mode);

    /* Made here, before any block is watched. */
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::AudioBuffer<float> longer (2, 2 * blockSize);
    juce::MidiBuffer none;
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);

    long allocations = 0;
    long writes = 0;
    /* `blocks` blocks; `each` runs before block k on the message thread's
     * side, `inside` on the audio thread's just before it, as the VST3
     * wrapper applies a host's changes -- what it allocates counted in
     * `writes`, what the block does in `allocations`. The message loop runs
     * between blocks. */
    const auto play = [&] (int blocks, bool bypassed = false, juce::AudioBuffer<float>* b = nullptr,
                           const std::function<void (int)>& each = {}, const std::function<void (int)>& inside = {})
    {
        auto& use = b != nullptr ? *b : buffer;
        for (int k = 0; k < blocks; ++k)
        {
            for (int ch = 0; ch < use.getNumChannels(); ++ch)
                for (int i = 0; i < use.getNumSamples(); ++i)
                    use.setSample (ch, i, 0.7f * (float) std::sin (0.031 * (double) (transport.sample + i)));
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
            transport.sample += use.getNumSamples();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
    };

    play (100);
    check (allocations == 0, "two seconds of playback, Time", allocations);

    allocations = 0;
    processor.editorOpened();
    play (100);
    check (allocations == 0, "playback with the window open: the picture and the Ground", allocations);

    /* Mode from the audio thread: to Spectral, back to Time, and to Spectral
     * again -- each a dip, an engine primed, and a request for the latency. */
    allocations = 0;
    writes = 0;
    play (120, false, nullptr, {}, [&] (int k) {
        if (k % 40 == 0)
            mode.setValueNotifyingHost ((k / 40) % 2 == 0 ? 1.0f : 0.0f);
    });
    check (allocations == 0, "Mode switched while it plays: the blocks, the dips, the engines", allocations);
    check (writes == 0, "Mode written on the audio thread: its request for the latency", writes);
    const bool told = processor.getLatencySamples() == processor.latencyForMode()
                      && processor.getLatencySamples() > 0;
    check (told, "... and the latency followed it on the message thread", processor.getLatencySamples());

    allocations = 0;
    play (100);
    check (allocations == 0, "two seconds of playback, Spectral", allocations);

    /* Every control moved while it plays, from the message thread's side: a
     * new value each block, so a glide is always running -- Mode among them,
     * which switches engines every few blocks. */
    allocations = 0;
    play (ni::vz::Processor::paramCount * 8, false, nullptr, [&] (int k) {
        auto& p = processor.parameter (k % ni::vz::Processor::paramCount);
        p.setValueNotifyingHost ((float) ((k / ni::vz::Processor::paramCount) % 5) / 4.0f);
    });
    check (allocations == 0, "every control automated while it plays", allocations);

    /* And from the audio thread's side, as a host's automation arrives. */
    allocations = 0;
    writes = 0;
    play (ni::vz::Processor::paramCount * 8, false, nullptr, {}, [&] (int k) {
        auto& p = processor.parameter (k % ni::vz::Processor::paramCount);
        p.setValueNotifyingHost ((float) ((k / ni::vz::Processor::paramCount) % 5) / 4.0f);
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
    check (allocations == 0, "a reactivation at another rate: every table made again", allocations);
    processor.prepareToPlay (sampleRate, blockSize);

    allocations = 0;
    play (20, true);
    check (allocations == 0, "the host's bypass: the engine listening", allocations);

    /* In and out of the bypass, block by block: every way back a resume. */
    allocations = 0;
    for (int k = 0; k < 20; ++k)
        play (1, k % 2 == 0);
    check (allocations == 0, "bypassed and back, again and again", allocations);

    /* The ceiling switched while it plays, from either side. */
    auto& ceiling = processor.parameter (ni::vz::param::ceiling);
    allocations = 0;
    play (20, false, nullptr, [&] (int k) { ceiling.setValueNotifyingHost ((float) (k % 2)); });
    check (allocations == 0, "Ceiling switched while it plays", allocations);
    allocations = 0;
    writes = 0;
    play (20, false, nullptr, {}, [&] (int k) { ceiling.setValueNotifyingHost ((float) (k % 2)); });
    check (allocations == 0, "Ceiling switched from the audio thread: the blocks", allocations);
    check (writes == 0, "... and the writes themselves", writes);

    allocations = 0;
    play (4, false, &longer);
    check (allocations == 0, "a block longer than the host announced", allocations);

    allocations = 0;
    processor.setStateInformation (saved.getData(), (int) saved.getSize());
    play (20);
    check (allocations == 0, "a set loaded while playing", allocations);

    /* A VOICE CAPTURED WHILE IT PLAYS, with Vocal all the way so it is
     * heard: read and measured on the capture's thread, applied at the top
     * of a block, and glided in. */
    using Status = ni::vz::Voice::Status;
    ni::vz::test::Scratch scratch ("vz_rt");
    const auto take = scratch / "take.wav";
    const bool written = ni::vz::test::writeWav (
        take, ni::vz::test::sing ({ 0, 2, 4 }, 1.15, 140.0, 0.6, sampleRate), sampleRate);
    check (written, "a recording to capture a voice from", 0);
    processor.parameter (ni::vz::param::vocal).setValueNotifyingHost (1.0f);
    allocations = 0;
    processor.capture (take);
    int waited = 0;
    while (processor.voice().status == Status::capturing && waited < 4000)
    {
        play (1);
        ++waited;
    }
    check (processor.voice().status == Status::captured, "a voice captured while it plays", waited);
    play (50);
    check (allocations == 0, "... arriving through the shell and gliding in", allocations);

    /* Voice automated, a voice captured: the morph remade as it moves. */
    auto& amount = processor.parameter (ni::vz::param::voice);
    allocations = 0;
    play (40, false, nullptr, [&] (int k) { amount.setValueNotifyingHost ((float) (k % 5) / 4.0f); });
    check (allocations == 0, "Voice automated while it plays", allocations);
    allocations = 0;
    writes = 0;
    play (40, false, nullptr, {}, [&] (int k) { amount.setValueNotifyingHost ((float) (k % 5) / 4.0f); });
    check (allocations == 0, "Voice automated from the audio thread: the blocks", allocations);
    check (writes == 0, "... and the writes themselves", writes);

    /* Forgotten, and restored with a set, while it plays. */
    juce::MemoryBlock withVoice;
    processor.getStateInformation (withVoice);
    allocations = 0;
    processor.forgetVoice();
    play (20);
    check (allocations == 0, "the voice forgotten while it plays", allocations);
    allocations = 0;
    processor.setStateInformation (withVoice.getData(), (int) withVoice.getSize());
    play (20);
    check (allocations == 0, "a set with a voice loaded while it plays", allocations);
    check (processor.voice().status == Status::captured, "... which restored the voice", 0);

    processor.editorClosed();
    processor.releaseResources();

    /* A mono track: its own instance, its own layout. */
    {
        ni::vz::Processor mono;
        auto layout = mono.getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        const bool laidOut = mono.setBusesLayout (layout);
        check (laidOut, "a mono layout is taken", 0);
        mono.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> one (1, blockSize);
        long monoAllocations = 0;
        for (int k = 0; k < 60; ++k)
        {
            for (int i = 0; i < blockSize; ++i)
                one.setSample (0, i, 0.5f * (float) std::sin (0.02 * (double) (k * blockSize + i)));
            if (k == 20)
                mono.parameter (ni::vz::param::mode).setValueNotifyingHost (1.0f);
            begin();
            mono.processBlock (one, none);
            monoAllocations += end();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
        check (monoAllocations == 0, "a mono track, in either engine", monoAllocations);
    }

    processor.setPlayHead (nullptr);
    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
