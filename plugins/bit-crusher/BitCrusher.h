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
 *                 sidechain
 *   a block       the shell taken (bc_shell_begin), every parameter pushed in
 *                 32-frame steps, ramped across the block, the channels crushed
 *                 in place, the shell given back -- which publishes the scope
 *                 when one is due
 *   latency       bc_latency, 32 samples, constant; bypassed, the engine passes
 *                 the input delayed by as much and fades between the two
 *   activation    prepare() posts the rate and a reset, so a host that
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

class Processor final : public ni::Processor,
                        public Model
{
public:
    static constexpr int paramCount = param::count;

    Processor();
    ~Processor() override;

    bool isBusesLayoutSupported (const BusesLayout&) const override;
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
    double steps (float* out, int count) override;
    const Scope& scope() override;
    double refreshPeriodMs() const override;
    double setting (int index) const override { return plain (index); }
    int levelCount() const override;
    void filterResponse (float* outDb, int count, double loHz, double hiHz) const override;

protected:
    /* The shell's prepareToPlay calls it: the rate and a reset posted. */
    void prepare (double sampleRate, int maximumBlockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    /* Bypassed, the engine passes the host's audio, delayed by the reported
     * latency, and the Ground keeps the host's time. */
    void processBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    /* Parameters are pushed in steps this long, ramped across the block. */
    static constexpr int rampFrames = 32;

    void run (juce::AudioBuffer<float>&, bool heard);
    double between (int index, double from, double to, double t) const;

    struct ShellDeleter
    {
        void operator() (BcShell* s) const { bc_shell_destroy (s); }
    };
    std::unique_ptr<BcShell, ShellDeleter> shell;
    std::array<juce::RangedAudioParameter*, paramCount> params {};
    std::array<bool, paramCount> toggles {};
    /* The values the last block ended on and this one ends on: the audio
     * thread's alone. */
    std::array<double, paramCount> last {}, target {};
    bool ramping = false;
    ni::GroundClock beat;

    /* The scope as the engine published it, and as the editor takes it. */
    std::unique_ptr<BcScope> published;
    Scope latest;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace ni::bc
