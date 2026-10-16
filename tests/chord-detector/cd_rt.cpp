// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Chord-Detector's audio callback allocates nothing, on the JUCE shell.
 *
 *   cd_rt      (run with alloc_guard inserted)
 *
 * The processor itself, as its VST3 wrapper drives it, under alloc_guard.c:
 * every malloc the calling thread makes inside processBlock is counted -- the
 * shell's C++, JUCE's, and the Rust engine's alike. The blocks cover what a
 * session does to it: a lane of chords changing every block, at offsets
 * inside it, with the pedal down and up, while the transport plays and while
 * it stands; every control moved from the message thread between blocks; a
 * panic (all sound off, all notes off) mid-chord; a reactivation, another
 * rate, the host's bypass (which still hears the lane), a mono track, a block
 * longer than the host announced, and a set loaded. Each must count zero.
 *
 * Between the blocks, on the message thread and not counted, the editor's
 * side is read as its window reads it -- the reading and the note events
 * drained -- so the engine's frames actually cross to a reader, and the test
 * checks they did. What the editor itself allocates converting them is the
 * message thread's business and deliberately not measured; nor is building
 * the blocks' MIDI, which is the host's.
 *
 * The guard is shown an allocation first, so a guard that was not inserted
 * -- and would count nothing ever -- fails rather than passes. macOS only:
 * the guard is a dyld interposer.
 */
#include "ChordDetector.h"

#include <dlfcn.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

using ni::chord_detector::ChordDetector;

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
    bool playing = true;
    double rate = sampleRate;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setBpm (120.0);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setTimeInSamples (sample);
        p.setPpqPosition ((double) sample / rate * 2.0);
        return p;
    }
};

/* A progression whose every step names something else: triads, sevenths, an
 * interval, a single note, a cluster with no name. */
const std::array<std::vector<int>, 8> progression { {
    { 60, 64, 67 },
    { 57, 60, 64, 67 },
    { 62, 65, 69, 72 },
    { 55, 59, 62, 65 },
    { 60, 67 },
    { 61 },
    { 60, 61, 62, 63 },
    { 53, 57, 60, 64 },
} };
} // namespace

int main()
{
    const juce::ScopedJuceInitialiser_GUI gui;
    std::printf ("cd_rt\n");

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

    ChordDetector processor;
    Transport transport;
    processor.setPlayHead (&transport);
    processor.prepareToPlay (sampleRate, blockSize);

    /* Made here, before any block is watched. */
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::AudioBuffer<float> longer (2, 2 * blockSize);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    std::array<ni::chord_detector::NoteEvent, 256> events {};

    /* What the editor has seen: how often the reading's texts changed, how
     * many note events it drained. Message thread. */
    std::uint32_t lastSerial = processor.reading().serial;
    long serials = 0, drained = 0;
    const auto readAsTheEditorDoes = [&] {
        const auto& r = processor.reading();
        if (r.serial != lastSerial)
        {
            lastSerial = r.serial;
            ++serials;
        }
        for (int n; (n = processor.takeNotes (events.data(), (int) events.size())) > 0;)
            drained += n;
        processor.key();
        processor.write (60);
    };

    /* The lane for block `step`: the last chord released and the next struck,
     * at offsets inside the block; the pedal down on every fourth, up on the
     * next. Built before the block is watched, as a host builds it. */
    const auto lane = [&] (int step, int frames) {
        midi.clear();
        const auto& from = progression[(size_t) ((step + 7) % (int) progression.size())];
        const auto& to = progression[(size_t) (step % (int) progression.size())];
        for (const int k : from)
            midi.addEvent (juce::MidiMessage::noteOff (1, k), frames / 4);
        int at = frames / 4 + 1;
        for (const int k : to)
            midi.addEvent (juce::MidiMessage::noteOn (1, k, (juce::uint8) 96), at++);
        if (step % 4 == 0)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), frames / 2);
        else if (step % 4 == 1)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), frames / 2);
    };

    long allocations = 0;
    int step = 0;
    const auto play = [&] (int blocks, bool bypassed = false, juce::AudioBuffer<float>* b = nullptr,
                           const std::function<void (int)>& each = {})
    {
        auto& use = b != nullptr ? *b : buffer;
        for (int k = 0; k < blocks; ++k)
        {
            use.clear();
            lane (step++, use.getNumSamples());
            if (each)
                each (k);
            begin();
            if (bypassed)
                processor.processBlockBypassed (use, midi);
            else
                processor.processBlock (use, midi);
            allocations += end();
            transport.sample += use.getNumSamples();
            readAsTheEditorDoes();
        }
    };

    play (100);
    check (allocations == 0, "a second of chords changing every block, playing", allocations);
    check (serials > 0, "... and the editor read the reading move", serials);
    check (drained > 0, "... and drained the notes", drained);

    allocations = 0;
    transport.playing = false;
    play (40);
    transport.playing = true;
    check (allocations == 0, "the same with the transport stopped", allocations);

    allocations = 0;
    processor.setPlayHead (nullptr);
    play (10);
    processor.setPlayHead (&transport);
    check (allocations == 0, "a host that gives no position at all", allocations);

    /* Every control moved while it plays, from the message thread's side: a
     * new value each block -- the key, the mode and the spelling retexting
     * every reading, Hold keeping or letting go. */
    const int n = ni::chord_detector::paramCount;
    allocations = 0;
    play (n * 8, false, nullptr, [&] (int k) {
        auto& p = processor.parameter (k % n);
        p.setValueNotifyingHost ((float) ((k / n + k) % 5) / 4.0f);
    });
    check (allocations == 0, "every control moved while it plays", allocations);

    /* A panic mid-chord: all sound off and all notes off on every channel,
     * with the chord still held. */
    allocations = 0;
    play (4);
    for (int k = 0; k < 4; ++k)
    {
        buffer.clear();
        midi.clear();
        for (int ch = 1; ch <= 16; ++ch)
        {
            midi.addEvent (juce::MidiMessage::allSoundOff (ch), k * 16);
            midi.addEvent (juce::MidiMessage::allNotesOff (ch), k * 16 + 1);
        }
        begin();
        processor.processBlock (buffer, midi);
        allocations += end();
        transport.sample += blockSize;
        readAsTheEditorDoes();
    }
    play (4);
    check (allocations == 0, "a panic: all sound off, all notes off", allocations);

    allocations = 0;
    processor.prepareToPlay (sampleRate, blockSize);
    play (20);
    check (allocations == 0, "a reactivation: the reset applied", allocations);

    allocations = 0;
    processor.prepareToPlay (96000.0, blockSize);
    transport.rate = 96000.0;
    play (20);
    check (allocations == 0, "a reactivation at another rate", allocations);
    processor.prepareToPlay (sampleRate, blockSize);
    transport.rate = sampleRate;

    allocations = 0;
    for (int k = 0; k < 20; ++k)
        play (1, k % 2 == 0);
    check (allocations == 0, "bypassed and back, again and again: the lane heard", allocations);

    allocations = 0;
    play (4, false, &longer);
    check (allocations == 0, "a block longer than the host announced", allocations);

    allocations = 0;
    processor.setStateInformation (saved.getData(), (int) saved.getSize());
    play (20);
    check (allocations == 0, "a set loaded while playing", allocations);

    processor.releaseResources();
    processor.setPlayHead (nullptr);

    {
        ChordDetector mono;
        auto layout = mono.getBusesLayout();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (mono.setBusesLayout (layout), "a mono layout is taken", 0);
        mono.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> one (1, blockSize);
        long monoAllocations = 0;
        for (int k = 0; k < 40; ++k)
        {
            one.clear();
            lane (k, blockSize);
            begin();
            mono.processBlock (one, midi);
            monoAllocations += end();
            mono.reading();
        }
        check (monoAllocations == 0, "a mono track", monoAllocations);
        mono.releaseResources();
    }

    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
