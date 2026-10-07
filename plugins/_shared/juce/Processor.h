// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What every Neon Ingvy plugin's processor is before it is a product: a JUCE
 * AudioProcessor with the house's answers to the questions every product
 * would otherwise answer on its own.
 *
 *   identity      the name is the build's (ni_add_juce_plugin: NAME); MIDI
 *                 in is the build's too (MIDI_IN); no MIDI out, no tail, one
 *                 program
 *   the block     processBlock is final: it runs the product's process()
 *                 under juce::ScopedNoDenormals, so no product forgets the
 *                 guard and no product pays for denormals
 *   the clock     readClock() reads the host's transport into a plain value,
 *                 allocation-free, for the engines' block clocks
 *   the state     getStateInformation and setStateInformation are final and
 *                 call writeState / readState, which a product overrides when
 *                 it must read an older build's format (the iPlug2 chunk);
 *                 the default is every parameter by its ID, plain values, in
 *                 JUCE's binary XML -- robust to a parameter added later
 *   dirty         nonParameterStateChanged() tells the host that state it
 *                 cannot see changed -- an import, a pasted pattern -- so the
 *                 set is marked unsaved
 *   VST3          getVST3ClientExtensions() is this, so a product that takes
 *                 over an older class answers getCompatibleParameterIds here
 *
 * THREADS. process() is the audio thread's: no allocation, no lock, no
 * system call, the engine reached only through its shell. The state calls and
 * nonParameterStateChanged() are the message thread's.
 */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ni
{

/* The host's transport for one block. `known` is false when the host gave
 * no position at all, which is not the same as a stopped transport. */
struct HostClock
{
    bool known = false;
    bool playing = false;
    double ppq = 0.0;
    double bpm = 0.0;
    int numerator = 0;
    int denominator = 0;
};

/* The transport from a play head, or an unknown clock for none. Audio thread;
 * allocates nothing. */
HostClock readClock (juce::AudioPlayHead*);

class Processor : public juce::AudioProcessor,
                  public juce::VST3ClientExtensions
{
public:
    explicit Processor (const BusesProperties&);
    ~Processor() override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) final;
    using juce::AudioProcessor::processBlock;

    void getStateInformation (juce::MemoryBlock&) final;
    void setStateInformation (const void* data, int sizeInBytes) final;

    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }

    /* The default state's root tag, and the format it writes. */
    static constexpr const char* stateTag = "NeonIngvy";
    static constexpr int stateFormat = 1;

protected:
    /* One block of the product. Audio thread. */
    virtual void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) = 0;

    /* The state, both ways. Message thread. readState returns whether it
     * understood what it was given; on false nothing was changed. */
    virtual void writeState (juce::MemoryBlock&);
    virtual bool readState (const void* data, size_t size);

    /* The host's parameters by their IDs: what the default state carries. */
    juce::RangedAudioParameter* parameterWithId (const juce::String& id) const;

    /* State the host cannot see changed: mark the set unsaved. Message
     * thread. */
    void nonParameterStateChanged();

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace ni
