// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain on the JUCE shell: the iPlug2 build's identity, parameters
 * and saved state, around the same Rust engine, with the native editor.
 *
 * WHAT CARRIES A LIVE SET ACROSS FROM THE iPlug2 BUILD, each in its place:
 *
 *   the class        the iPlug2 class ID itself (CMakeLists.txt: IPLUG2_CLASS,
 *                    JUCE_VST3_COMPONENT_CLASS), so a set finds this build as
 *                    it found the old one
 *   the bundle       NISideChain.vst3, the old bundle's name, so installing
 *                    this one replaces it instead of standing beside it with
 *                    the same class
 *   the parameters   the same fifteen at the same IDs (Params.h, legacy
 *                    parameter IDs), holding plain values exactly
 *                    (ni::Parameter); Bypass is the host's, ID 15 here and
 *                    65536 there
 *   the state        the iPlug2 chunk, fifteen values and the bypass after
 *                    them (ni::nist, Params.h's layout), read and written
 *                    byte for byte; the bypass is restored too, which iPlug2
 *                    left in its controller
 *
 * WHAT DOES NOT CARRY ACROSS, and is said to the user (docs/live.md): the
 * iPlug2 build exported 130 MIDI-CC parameters (VST3 IDs 65538-65667) for
 * its MIDI input. JUCE maps MIDI CCs onto parameters of its own, with other
 * IDs, so automation drawn on one of those would not find its lane. Nothing
 * in the plugin read them as parameters; the CCs themselves still arrive.
 *
 * THE BUSES: the main input -- mono or stereo, the output the same -- and a
 * sidechain input, the key, mono or stereo, which a host connects or not.
 * Live shows it as the device's sidechain section. Whether the key is
 * connected is the bus's state, which only this side can tell: an unpatched
 * bus and a silent one are the same zeroes, and the editor says which.
 *
 * MIDI IN, WITH ITS SAMPLE OFFSET: a note lands on its own sample rather than
 * on the top of whatever block the host uses -- jitter nothing downstream
 * could compensate. CC 120 (All Sound Off) and CC 123 (All Notes Off) open
 * the gate whatever the note filter says (sc_core_on_midi): the panic. A
 * host sends CCs to a VST3 through its MIDI-CC mapping, which JUCE's wrapper
 * turns back into the controller events this reads, so the path is the
 * ordinary MIDI one. Nothing is forwarded: a ducker echoing its trigger
 * notes would arm the instrument after it.
 *
 * THE ENGINE BELONGS TO THE AUDIO THREAD (sc_shell.h). A block takes it
 * between sc_shell_begin and sc_shell_end: the host's values are pushed, the
 * key and the MIDI go in, the main channels are ducked in place, and -- while
 * an editor is open -- the dry signal, the gain and the sweep go to the
 * scope. Nothing there allocates, locks or parses. Bypassed, the engine still
 * runs, on a copy, so it keeps time and hears every MIDI message -- a Gate
 * note released while bypassed does not hold the duck down afterwards, and a
 * panic is a panic -- and the host's audio passes untouched.
 */
#pragma once

#include "GroundClock.h"
#include "Parameter.h"
#include "Params.h"
#include "Processor.h"
#include "ni/Scope.h"
#include "sc_shell.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace ni::sc
{

class EngineModel;

class Processor final : public ni::Processor
{
public:
    /*
     * The capture's width. The axis is ONE CYCLE, from the engine's sweep, so
     * the shape the user drags is drawn directly above the audio it shaped.
     * 512 columns is about a pixel and a half each at the plot's width.
     */
    static constexpr int scopeColumns = 512;
    using Scope = ni::Scope<scopeColumns>;

    Processor();
    ~Processor() override;

    /* Main in mono or stereo, the output the same; the key off, mono or
     * stereo. */
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    void editorOpened() override;
    void editorClosed() override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypass; }

    /* ---- what the model and the tests reach */

    sc_shell_t* engine() const noexcept { return shell.get(); }
    ni::Parameter& parameter (int index) const noexcept { return *params[(std::size_t) index]; }
    const Scope& scope() const noexcept { return capture; }
    ni::GroundClock& ground() noexcept { return beat; }
    EngineModel& model() noexcept { return *editorModel; }
    /* Whether the host connected a key, as of the last block. Any thread. */
    bool keyConnected() const noexcept { return keyed.load (std::memory_order_relaxed); }

protected:
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void writeState (juce::MemoryBlock&) override;
    bool readState (const void* data, size_t size) override;

private:
    /* One block through the engine. `heard`: the host's buffer is ducked in
     * place and the scope fed; otherwise the engine runs on a copy. */
    void run (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const ni::HostClock&, bool heard);

    struct ShellDeleter
    {
        void operator() (sc_shell_t* s) const noexcept { sc_shell_destroy (s); }
    };
    std::unique_ptr<sc_shell_t, ShellDeleter> shell;

    std::array<ni::Parameter*, kNumParams> params {};
    ni::Parameter* bypass = nullptr;

    Scope capture;
    ni::GroundClock beat;
    std::atomic<bool> capturing { false };
    std::atomic<bool> keyed { false };
    double sampleRate = 44100.0;
    /* The dry input, the engine's gain and sweep for the capture, and the
     * copies a bypassed block or a mono track's second channel run on. Sized
     * in prepareToPlay, never on the audio thread; a longer block is
     * processed in chunks of this. */
    std::vector<float> dry, gain, sweep, copyL, copyR;

    std::unique_ptr<EngineModel> editorModel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace ni::sc
