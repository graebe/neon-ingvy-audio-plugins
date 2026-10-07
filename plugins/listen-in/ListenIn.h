// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In on the JUCE shell: a tap that other plugins can read. Audio
 * passes through bit for bit and is published, stereo, on one of sixteen
 * shared-memory buses (engines/audio-bus) a Spectrogram can listen to.
 *
 * WHAT CARRIES A LIVE SET ACROSS FROM THE iPlug2 BUILD, each in its place:
 *
 *   the class        the iPlug2 class ID itself (CMakeLists.txt: IPLUG2_CLASS)
 *   the bundle       NIListenIn.vst3, the old bundle's name, so installing this
 *                    one replaces it instead of standing beside it
 *   the parameter    Bus at ID 0, holding its plain value exactly
 *                    (ni::Parameter, State.h); Bypass is the host's, ID 1 here
 *                    and 65536 there, which Live maps by its flag
 *   the state        the iPlug2 chunk, the bus and its name (ni::nist,
 *                    State.h's layout), read in every layout it ever had and
 *                    written as the last one wrote it; the bypass it carries
 *                    is restored too, which iPlug2 left in its controller
 *
 * A CLAIM IS TWO HALVES, AND EACH THREAD HOLDS ONLY ITS OWN. Claiming maps
 * shared memory and releasing unlinks it, so neither happens on the audio
 * thread: every other thread only records what it wants -- the host's rate,
 * a state load, a name -- and the message thread acts on it (serviceBus, at
 * 30 Hz as iPlug2's idle timer did). That thread keeps the WRITER (the name,
 * the rate); the audio thread's PUSHER crosses to it through the shell's
 * handoff, which frees a replaced one only once no block holds it. The bus is
 * released with the last half. Nothing is claimed before the host first
 * prepares the plugin, since a bus is announced at a sample rate.
 *
 * THE BLOCK (process): the Ground ticked from the host's clock, the input
 * interleaved into a staging buffer sized up front -- a mono input published
 * on both sides -- and pushed to the bus, the peak kept, and the audio left
 * exactly as it came, in place. Nothing there allocates, locks or makes a
 * system call. Bypassed -- the host's Bypass on, which the block reads itself,
 * or a host bypassing it -- nothing is published and the meter falls.
 */
#pragma once

#include "GroundClock.h"
#include "Parameter.h"
#include "Processor.h"
#include "State.h"
#include "audio_bus.h"
#include "Model.h"
#include "shell_handoff.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ni::li
{

class EngineModel;

class Processor final : public ni::Processor,
                        private juce::Timer
{
public:
    /* The block staged for the bus. A host may hand over more than it
     * announced, so a block is pushed in chunks of this, never resized. */
    static constexpr int stageFrames = 4096;

    Processor();
    ~Processor() override;

    /* Stereo in, stereo out, as the iPlug2 build's "2-2"; a mono track's
     * stereo pair is published as it comes. */
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    void editorOpened() override;
    void editorClosed() override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypass; }

    /* ---- what the model and the tests reach */

    ni::Parameter& busParameter() const noexcept { return *bus; }
    ni::GroundClock& ground() noexcept { return beat; }
    EngineModel& model() noexcept { return *editorModel; }

    /* The bus's state as the message thread last decided it. Message thread. */
    Status status() const noexcept { return state; }
    /* The input's peak, linear, 0..1, decaying rather than reset. Any thread. */
    float peak() const noexcept { return level.load (std::memory_order_relaxed); }
    /* The name a save writes, a load not yet taken included. Any thread but
     * the audio one. */
    std::string label() const { return session.label(); }
    /* A name typed in the editor: kept (wire::parse_label), handed to the
     * bus at once, and the set marked unsaved. Message thread. */
    void editLabel (const std::string& typed);

    /* What the timer does at 30 Hz, at once: claims, releases and retunes the
     * bus to match what the other threads asked for, and hands it a changed
     * name. Message thread. */
    void serviceBus();

protected:
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void writeState (juce::MemoryBlock&) override;
    bool readState (const void* data, size_t size) override;

private:
    void timerCallback() override { serviceBus(); }
    /* A block that publishes nothing: the meter falls. Audio thread. */
    void fall() noexcept;
    /* The name the session changed, to the writer. Message thread. */
    void serviceLabel();

    ni::Parameter* bus = nullptr;
    ni::Parameter* bypass = nullptr;

    /* The pusher, lent to process(). */
    shell_handoff_t* const handoff;
    std::atomic<std::uint32_t> rate { 0 };      /* the host's, from prepareToPlay */
    std::atomic<bool> resetSeen { false };      /* a prepare: retry, or retune */
    Session session;
    std::atomic<float> level { 0.0f };

    /* Message thread only. */
    abus_writer_t* writer = nullptr;
    Status state = Status::idle;
    int triedSlot = 0;     /* the bus last asked for, won or not */
    bool waiting = false;  /* the old pusher is still held by a block */
    bool reclaim = false;  /* a state load: claim afresh */
    std::string named;     /* the name the writer was given */

    ni::GroundClock beat;
    std::vector<float> stage; /* interleaved, pre-sized, never resized */

    std::unique_ptr<EngineModel> editorModel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace ni::li
