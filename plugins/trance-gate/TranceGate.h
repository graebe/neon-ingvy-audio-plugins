// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate on the JUCE shell: the iPlug2 build's identity, parameters
 * and saved state, around the same Rust engine, with the native editor.
 *
 * WHAT CARRIES A LIVE SET ACROSS FROM THE iPlug2 BUILD, each in its place:
 *
 *   the class        the iPlug2 class ID itself (CMakeLists.txt: IPLUG2_CLASS,
 *                    JUCE_VST3_COMPONENT_CLASS), so a set finds this build as
 *                    it found the old one -- proven in Live (UT2)
 *   the bundle       NITranceGate.vst3, the old bundle's name, so installing
 *                    this one replaces it instead of standing beside it with
 *                    the same class
 *   the parameters   the same fifteen at the same IDs (Params.h, legacy
 *                    parameter IDs), holding plain values exactly
 *                    (ni::Parameter); Bypass is the host's, ID 15 here and
 *                    65536 there, which Live maps by its flag
 *   the state        the iPlug2 chunk, read in every layout it ever had and
 *                    written as the last one wrote it (ni::nist, Params.h's
 *                    layout); the bypass it carries is restored too, which
 *                    iPlug2 left in its controller
 *
 * THE ENGINE BELONGS TO THE AUDIO THREAD (tg_shell.h). process() takes it for
 * one block between tg_shell_begin and tg_shell_end: the host's values are
 * pushed, the transport and meter go in, the two channels are gated in place,
 * and -- while an editor is open -- the dry and gated signal go to the scope.
 * Nothing there allocates, locks or parses. Every other thread posts edits and
 * reads what the engine published: the state calls (any thread but the audio
 * one), the slot follow and the editor's model (the message thread).
 *
 * A SLOT SWITCH RECALLS A WHOLE SOUND. When the engine says a switch, a paste
 * or an import landed (tg_shell_take_params), the host's parameters follow it
 * on the message thread, so automation lanes, Live's panel and the editor all
 * show the recalled values.
 */
#pragma once

#include "GroundClock.h"
#include "Parameter.h"
#include "Params.h"
#include "Processor.h"
#include "ni/Scope.h"
#include "tg_shell.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace ni::tg
{

class EngineModel;

class Processor final : public ni::Processor,
                        private juce::Timer
{
public:
    /* The Signal capture's columns: column k is pattern phase k / columns. */
    static constexpr int scopeColumns = 256;
    using Scope = ni::Scope<scopeColumns>;

    Processor();
    ~Processor() override;

    /* Stereo in, stereo out, and nothing else: the iPlug2 build's "2-2". */
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    void editorOpened() override;
    void editorClosed() override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypass; }

    /* ---- what the model and the tests reach */

    tg_shell_t* engine() const noexcept { return shell.get(); }
    ni::Parameter& parameter (int index) const noexcept { return *params[(std::size_t) index]; }
    /* Every host value on the engine's numeric wire, in tg_param_t order: what
     * a block pushes, and what a save or an export is made with. Any thread. */
    void engineValues (double (&out)[kNumParams]) const noexcept;
    const Scope& scope() const noexcept { return capture; }
    ni::GroundClock& ground() noexcept { return beat; }
    EngineModel& model() noexcept { return *editorModel; }
    bool editorOpen() const noexcept { return capturing.load (std::memory_order_relaxed); }

    /* The pattern, a slot, a paste or an import changed what a save writes,
     * and the host cannot see it: mark the set unsaved. Message thread. */
    void stateChanged() { nonParameterStateChanged(); }

    /* The slot follow, at once: what the timer does at 30 Hz. Whether the
     * engine had a switch, a paste or an import to follow. Message thread. */
    bool followEngine();

protected:
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void writeState (juce::MemoryBlock&) override;
    bool readState (const void* data, size_t size) override;

private:
    void timerCallback() override { followEngine(); }

    struct ShellDeleter
    {
        void operator() (tg_shell_t* s) const noexcept { tg_shell_destroy (s); }
    };
    std::unique_ptr<tg_shell_t, ShellDeleter> shell;

    std::array<ni::Parameter*, kNumParams> params {};
    ni::Parameter* bypass = nullptr;

    Scope capture;
    ni::GroundClock beat;
    std::atomic<bool> capturing { false };
    double sampleRate = 44100.0;
    /* The dry input and the engine's sweep, for the capture. Sized in
     * prepareToPlay, never on the audio thread; a longer block is processed
     * in chunks of this. */
    std::vector<float> dry, sweep;

    std::unique_ptr<EngineModel> editorModel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace ni::tg
