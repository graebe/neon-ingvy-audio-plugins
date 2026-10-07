// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Spectrogram's audio callback allocates nothing, on the JUCE shell.
 *
 *   sg_rt <tests/fixtures/iplug2>      (run with alloc_guard inserted)
 *
 * The processor itself, as its VST3 wrapper drives it, under alloc_guard.c:
 * every malloc the calling thread makes inside processBlock is counted -- the
 * shell's C++, JUCE's, and the Rust receiver's alike. Between the blocks the
 * message thread does what its timer does (service: the receiver rebuilt,
 * the session applied, the columns drained or taken), so the blocks see the
 * receiver in every state a session puts it in: a loaded set, playback with
 * the window open and the editor taking columns, the session's commands
 * landing, the host's bypass, a new sample rate -- the blocks while its
 * receiver is pending, and after -- and a second load. Each must count zero.
 *
 * The guard is shown an allocation first, so a guard that was not inserted
 * -- and would count nothing ever -- fails rather than passes. macOS only:
 * the guard is a dyld interposer.
 */
#include "EngineModel.h"
#include "SpectrogramProcessor.h"

#include <dlfcn.h>
#include <unistd.h>

#include <cmath>
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

constexpr int blockSize = 512;

struct Transport final : juce::AudioPlayHead
{
    double rate = 48000.0;
    juce::int64 sample = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true);
        p.setBpm (120.0);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setTimeInSamples (sample);
        p.setPpqPosition ((double) sample / rate * 2.0);
        return p;
    }
};
} // namespace

int main (int argc, char* argv[])
{
    if (argc != 2)
    {
        std::fprintf (stderr, "usage: sg_rt <fixtures-dir>\n");
        return 2;
    }
    /* A bus namespace of this run's own: the session's buses are probed. */
    const auto ns = "sg_rt." + std::to_string ((long long) getpid());
    setenv ("NIA_BUS_NS", ns.c_str(), 1);
    const juce::ScopedJuceInitialiser_GUI gui;
    const auto fixtures = juce::File (argv[1]).getChildFile ("NISpectrogram");
    std::printf ("sg_rt\n");

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

    ni::spectrogram::Processor processor;
    Transport transport;
    processor.setPlayHead (&transport);
    processor.prepareToPlay (transport.rate, blockSize);
    processor.service();

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    std::vector<std::uint8_t> levels ((std::size_t) (ni::spectrogram::Model::maxColumns * 256)),
        clash (levels.size());
    double phase = 0.0;
    long allocations = 0;
    const auto play = [&] (int blocks, bool bypassed = false)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto v = (float) (0.5 * std::sin (phase));
                phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / transport.rate;
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            begin();
            if (bypassed)
                processor.processBlockBypassed (buffer, midi);
            else
                processor.processBlock (buffer, midi);
            allocations += end();
            transport.sample += blockSize;
            /* The message thread's tick, between blocks, as a host interleaves
             * them; and the editor's take while a window is open. */
            if (b % 4 == 3)
            {
                processor.service();
                if (processor.editorOpen())
                {
                    int clashCount = 0;
                    processor.model().takeColumns (levels.data(), clash.data(),
                                                   ni::spectrogram::Model::maxColumns, clashCount);
                }
            }
        }
    };
    const auto load = [&] (const char* scenario)
    {
        juce::MemoryBlock state;
        fixtures.getChildFile (juce::String (scenario) + ".component.bin").loadFileAsData (state);
        processor.setStateInformation (state.getData(), (int) state.getSize());
    };

    load ("session");
    play (200);
    check (allocations == 0, "a loaded session, then two seconds of playback", allocations);

    allocations = 0;
    processor.editorOpened();
    play (100);
    check (allocations == 0, "playback with the window open: the columns and the Ground", allocations);

    allocations = 0;
    auto& model = processor.model();
    model.setRange (200.0f, 4000.0f);
    model.setLook ({ 0, 1 }, 0, 1, true, { 4 });
    model.setClashCriteria (-50.0f, 9.0f);
    play (40);
    check (allocations == 0, "the session's commands land: a range, a look, a clash", allocations);

    allocations = 0;
    play (20, true);
    check (allocations == 0, "the host's bypass", allocations);

    allocations = 0;
    processor.prepareToPlay (44100.0, blockSize);
    transport.rate = 44100.0;
    for (int b = 0; b < 8; ++b)
    {
        begin();
        processor.processBlock (buffer, midi);
        allocations += end();
    }
    processor.service();
    play (40);
    check (allocations == 0, "a new rate: the blocks while its receiver is built, and after", allocations);

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
