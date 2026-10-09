// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bit-Crusher: a continuous signal made discrete, in amplitude and in time.
 *
 * THE ENGINE IS RUST (engines/bit-crusher): the steps, the hold, the filters,
 * the parameter table and its text. This file is the shell around it, and
 * does what a shell does:
 *
 *   parameters    declared from the engine's table (bc_param_*): ten numbers,
 *                 each on the travel the table gives it -- Rate in equal
 *                 ratios -- and printed and read in the engine's words; three
 *                 switches. The IDs are the engine's keys, the state the
 *                 default ni::Processor one (every parameter by its ID)
 *   the buses     mono in to mono out, or stereo to stereo; no MIDI, no
 *                 sidechain, no latency
 *   a block       the shell taken (bc_shell_begin), every parameter pushed
 *                 (cheap when unchanged), the channels crushed in place, the
 *                 shell given back -- which publishes the scope when one is due
 *   activation    prepareToPlay posts the rate and a reset, so a host that
 *                 stopped and started the plugin hears nothing it held before
 *   the editor    reads the engine's scope and renders the staircase through
 *                 the Model, on the message thread; the Ground's clock runs
 *                 while a window is open
 *
 * THE PANIC. On the Move, CC 120/123 reset the engine (bc-move); a VST3 in
 * Live is told to start afresh by being deactivated, which prepareToPlay
 * answers with the same reset.
 *
 * The audio thread allocates nothing: the engine does not, and nothing here
 * does on its way there (tests/bit-crusher/bc_rt.cpp holds it to that).
 */
#pragma once

#include "GroundClock.h"
#include "Model.h"
#include "Processor.h"
#include "bc_shell.h"

#include <array>
#include <memory>

namespace ni::bc
{

class BitCrusher final : public ni::Processor,
                         public Model
{
public:
    static constexpr int paramCount = param::count;

    BitCrusher();
    ~BitCrusher() override;

    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    void editorOpened() override;
    void editorClosed() override;

    /* A parameter's value in its own unit, as the engine takes it. Any
     * thread. */
    double plain (int index) const;

    /* ---- the Model, for the editor: message thread */
    int numParameters() const override { return paramCount; }
    juce::RangedAudioParameter& parameter (int index) override { return *params[(size_t) index]; }
    int takeRings (float* strengths, int capacity) override;
    bool motion() const override;
    void setMotion (bool on) override;
    bool bipolar (int index) const override;
    void steps (float* out, int count) override;
    const Scope& scope() override;

protected:
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    /* Bypassed, the host's audio passes untouched and the Ground keeps the
     * host's time. */
    void processBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    struct ShellDeleter
    {
        void operator() (BcShell* s) const { bc_shell_destroy (s); }
    };
    std::unique_ptr<BcShell, ShellDeleter> shell;
    std::array<juce::RangedAudioParameter*, paramCount> params {};
    ni::GroundClock beat;

    /* The scope as the engine published it, and as the editor takes it. */
    std::unique_ptr<BcScope> published;
    Scope latest;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BitCrusher)
};

} // namespace ni::bc
