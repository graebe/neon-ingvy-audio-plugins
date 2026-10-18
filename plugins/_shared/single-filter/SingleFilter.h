// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A single filter: one NI Filter Designer node as a plugin of its own -- the
 * shell all nine products share (NI High Pass, NI Low Pass, NI Band Pass,
 * NI Notch, NI Bell, NI Low Shelf, NI High Shelf, NI Tilt, NI All Pass).
 * Each product's plugins/<product>/ is its build and one line: the shape it
 * makes this processor with.
 *
 * THE ENGINE IS RUST (engines/single-filter): the filter, the product's
 * parameter table and its text, and the picture the editor draws. This file
 * is the shell around it, and does what a shell does:
 *
 *   parameters    declared from the product's table (sf_param_*(shape, …)):
 *                 five to ten of them, numbers printed and read in the
 *                 engine's words, choices of its labels. The IDs are the
 *                 engine's keys, the state the default ni::Processor one
 *   the bypass    the shared Bypass (ni::bypassSpec), the host's own: the
 *                 shell crossfades it over 10 ms (ni::Processor). For the
 *                 first two seconds bypassed the engine keeps filtering a
 *                 copy of the input, so a bypass flicked off and on again
 *                 comes back with its filter's state current; after that it
 *                 idles -- it only listens, for the analyzer -- and the block
 *                 that lifts the bypass starts it from a reset, under the
 *                 same fade
 *   the buses     mono in to mono out, or stereo to stereo; no MIDI, no
 *                 sidechain; no latency, ever (minimum phase)
 *   the tail      the engine's (sf_tail_seconds): how long the filter rings
 *                 on after the input stops, to -60 dB, at most 30 s
 *   a block       the shell taken (sf_shell_begin), the parameters that
 *                 moved since the last block pushed, the channels processed
 *                 in place, the shell given back. Automation lands once a
 *                 block: JUCE's VST3 wrapper hands a plugin the last point of
 *                 each parameter's queue
 *   activation    prepareToPlay posts the rate and a reset, so a host that
 *                 stopped and started the plugin hears nothing it held before
 *   the editor    reads the picture through the Model, on the message thread:
 *                 sf_shell_read designs it from the values the host holds,
 *                 so what is drawn is what plays; the analyzer's rings are
 *                 kept only while a window is open and the analyzer is on
 *
 * THE PANIC. On the Move, CC 120/123 reset the engine (sf-move); a VST3 in
 * Live is told to start afresh by being deactivated, which prepareToPlay
 * answers with the same reset.
 *
 * The audio thread allocates nothing: the engine does not, and nothing here
 * does on its way there (tests/single-filter/sf_rt.cpp holds it to that).
 */
#pragma once

#include "Model.h"
#include "Parameter.h"
#include "Processor.h"
#include "sf_shell.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace ni::sf
{

class Processor final : public ni::Processor,
                        public Model
{
public:
    /* The product whose filter is `shape` (SF_SHAPE_*). */
    explicit Processor (int shape);
    ~Processor() override;

    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void releaseResources() override {}

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypass; }
    double getTailLengthSeconds() const override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    void editorOpened() override;
    void editorClosed() override;

    /* A parameter's value in its own unit, as the engine takes it. Any
     * thread. */
    double plain (int index) const;

    /* ---- the Model, for the editor: message thread */
    int shape() const override { return product; }
    juce::String productName() const override;
    int indexOf (int field) const override;
    int numParameters() const override { return count; }
    juce::RangedAudioParameter& parameter (int index) override { return *params[(size_t) index]; }
    juce::RangedAudioParameter& bypassParameter() override { return *bypass; }
    bool bipolar (int index) const override;
    const Picture& picture() override;
    unsigned uses (int order, int family) const override;
    int orderPlayed (int family, int order) const override;
    double qForEdge (double freq, double edgeHz) const override;

protected:
    /* The shell's prepareToPlay calls this first (ni::Processor). */
    void prepare (double sampleRate, int maximumBlockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    /* Bypassed: JUCE's own pass-through, the engine running on a copy. */
    void processBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    /* One block through the engine: in place when `heard`, on a copy of the
     * input when bypassed, until it idles. */
    void run (juce::AudioBuffer<float>&, bool heard);
    /* The parameters that moved since the last block, to the engine. */
    void push (SfCore*);
    /* The analyzer's rings are kept while a window shows them. Message
     * thread. */
    void watch();
    std::array<double, SF_PARAM_MAX> values() const;

    struct ShellDeleter
    {
        void operator() (SfShell* s) const { sf_shell_destroy (s); }
    };
    const int product;
    const int count;
    std::unique_ptr<SfShell, ShellDeleter> shell;
    std::array<juce::RangedAudioParameter*, SF_PARAM_MAX> params {};
    ni::Parameter* bypass = nullptr;
    /* The bypassed input's copy, sized in prepare. */
    std::array<std::vector<float>, 2> listening;
    /* The rate prepare was given, for the tail; 0 before. */
    std::atomic<double> rate { 0.0 };
    /* Each parameter's value (0..1) as last pushed; NaN, so pushed, after
     * prepare. Audio thread. */
    std::array<float, SF_PARAM_MAX> pushed {};
    /* How long the engine filters a bypassed copy before it idles. */
    static constexpr double idleAfterSeconds = 2.0;
    std::int64_t bypassedFor = 0, idleAfter = 0;
    bool idle = false;
    bool windowOpen = false, watching = false;

    std::unique_ptr<SfPicture> published;
    Picture latest;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace ni::sf
