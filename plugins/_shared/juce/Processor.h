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
 *   prepare       prepareToPlay is final: it sizes what the shell itself
 *                 needs, then calls the product's prepare()
 *   the block     processBlock is final: it runs the product's process()
 *                 under juce::ScopedNoDenormals, so no product forgets the
 *                 guard and no product pays for denormals. Bypassed blocks
 *                 go to processBypassed(), JUCE's own by default -- a host
 *                 bypassing the plugin itself, and the product's own bypass
 *                 parameter (getBypassParameter) on, which JUCE's VST3
 *                 wrapper leaves to processBlock. A product whose engine must
 *                 keep hearing MIDI while bypassed -- or its held notes
 *                 stick -- overrides it
 *   the fade      the product's own Bypass moving is a 10 ms crossfade
 *                 between what process() makes and the input, never a jump
 *                 (processBlock); a product whose process() leaves the audio
 *                 as it came says so (passesAudioThrough) and switches at the
 *                 block
 *   the clock     readClock() reads the host's transport into a plain value,
 *                 allocation-free, for the engines' block clocks: the
 *                 position, the tempo, the meter and the loop
 *   the state     getStateInformation and setStateInformation are final and
 *                 call writeState / readState, which a product overrides when
 *                 it must read an older build's format (the iPlug2 chunk);
 *                 the default is every parameter by its ID, plain values, in
 *                 JUCE's binary XML -- robust to a parameter added later --
 *                 and whatever the product adds beside them
 *                 (writeExtraState / readExtraState)
 *   unread        a state readState refused -- a newer build's, a damaged
 *                 one -- is not lost by saving: its bytes are kept and saved
 *                 back unchanged until a state is read or the user edits
 *                 (stateUnread)
 *   dirty         nonParameterStateChanged() tells the host that state it
 *                 cannot see changed -- an import, a pasted pattern -- so the
 *                 set is marked unsaved
 *   VST3          getVST3ClientExtensions() is this, so a product that takes
 *                 over an older class answers getCompatibleParameterIds here
 *
 * THREADS. process() is the audio thread's: no allocation, no lock, no
 * system call, the engine reached only through its shell. The state calls
 * come on whatever thread the host chooses -- hosts load a set's state off the
 * main thread, which is why nothing in them may touch an editor -- and never
 * on the audio thread. nonParameterStateChanged() is the message thread's.
 */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace ni
{

/* The host's transport for one block. `known` is false when the host gave
 * no position at all, which is not the same as a stopped transport; `ppq` is
 * NaN when it gave a position without a musical one. `looping` with the
 * cycle's start and end in beats, as the host reports them; what a cycle
 * that cannot be played means is ni::wire::host_loop's to decide.
 * `timeInSamples` is the block's first sample on the host's timeline,
 * meaningful only while `timed`: playing, with a position in samples -- the
 * one clock two tracks' blocks share exactly (the audio bus stamps by it). */
struct HostClock
{
    bool known = false;
    bool playing = false;
    double ppq = 0.0;
    double bpm = 0.0;
    int numerator = 0;
    int denominator = 0;
    bool looping = false;
    double loopStart = 0.0;
    double loopEnd = 0.0;
    bool timed = false;
    std::int64_t timeInSamples = 0;
};

/* The transport from a play head, or an unknown clock for none. Audio thread;
 * allocates nothing. */
HostClock readClock (juce::AudioPlayHead*);

class Processor : public juce::AudioProcessor,
                  public juce::VST3ClientExtensions,
                  private juce::AudioProcessorListener
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
    /* The one program has a name, "Default" as iPlug2's builds called theirs:
     * the VST3 wrapper lists it as a program list, and Steinberg's validator
     * fails a program without one. */
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) final;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) final;
    using juce::AudioProcessor::processBlock;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) final;
    using juce::AudioProcessor::processBlockBypassed;

    void getStateInformation (juce::MemoryBlock&) final;
    void setStateInformation (const void* data, int sizeInBytes) final;

    /*
     * WHETHER THE STATE THE HOST GAVE COULD NOT BE READ: a set saved by a
     * newer build, or one damaged. The instance then plays its defaults, but
     * it does not throw the set away: getStateInformation hands back the
     * bytes it was given, unchanged, so opening the set here and saving it
     * loses nothing. That lasts until a state is read, or the user edits --
     * a gesture on any parameter, or nonParameterStateChanged() -- and the
     * instance's own state is what there is to save. An editor says so
     * ("made by a newer build") while this is true. Any thread.
     */
    bool stateUnread() const;

    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }

    /* An ni::PluginEditor opened, or is closing: what a product runs only
     * while a window shows it (a scope). JUCE's own editorBeingDeleted is
     * not virtual. Message thread. */
    virtual void editorOpened() {}
    virtual void editorClosed() {}

    /* The default state's root tag, and the format it writes. */
    static constexpr const char* stateTag = "NeonIngvy";
    static constexpr int stateFormat = 1;

protected:
    /* The host's rate and longest block, before the first block at them: what
     * a product sizes its buffers by. The host's thread, never the audio
     * one. */
    virtual void prepare (double sampleRate, int maximumExpectedSamplesPerBlock) = 0;

    /* One block of the product. Audio thread. */
    virtual void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) = 0;

    /* One block while the plugin is bypassed, by the host or by its own
     * bypass parameter: JUCE's own handling (audio through, a latency kept)
     * unless a product says otherwise. Audio thread. */
    virtual void processBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&);

    /* Whether process() leaves the audio exactly as it came -- an analyser,
     * a tap. Its Bypass then has nothing to fade: it switches at the block,
     * and process() is never run for a block that is bypassed. */
    virtual bool passesAudioThrough() const noexcept { return false; }

    /* The state, both ways, on any thread but the audio one: touch no
     * component. readState returns whether it understood what it was given;
     * on false nothing was changed. */
    virtual void writeState (juce::MemoryBlock&);
    virtual bool readState (const void* data, size_t size);

    /* What a product keeps beside its parameters in the default state: its
     * own children of the root, written after the parameters and handed back
     * after they are read -- a set saved without them reads them as absent.
     * The state's threads (above). */
    virtual void writeExtraState (juce::XmlElement&) {}
    virtual void readExtraState (const juce::XmlElement&) {}

    /* THE PRODUCT'S OWN STATE VERSION, written beside the codec's `format`:
     * raised when the meaning of what the product saves changes -- a range
     * reinterpreted, an extra element restructured -- never for a parameter
     * appended (that reads as its default). A state saved before versions
     * were written reads as 1. A newer one is refused. */
    virtual int stateVersion() const { return 1; }

    /* An older state brought up to stateVersion(), in place, before any of it
     * is applied: `from` is the version it was written at. False refuses it,
     * which changes nothing; the default refuses, so a product that raises
     * its version says how its older sets read. */
    virtual bool migrateState (juce::XmlElement&, int /* from */) { return false; }

    /* The host's parameters by their IDs: what the default state carries. */
    juce::RangedAudioParameter* parameterWithId (const juce::String& id) const;

    /* State the host cannot see changed: mark the set unsaved. Message
     * thread. */
    void nonParameterStateChanged();

private:
    /*
     * THE BYPASS FADE: where the output stands between what process() makes
     * (0) and the input (1), moved by `step` a sample towards wherever the
     * Bypass is. The input is kept in `dry` -- `dryChannels` channels of
     * `dryFrames`, allocated in prepareToPlay -- while a fade runs, since
     * process() writes over it in place.
     */
    static constexpr double bypassFadeSeconds = 0.010;
    std::vector<float> dry;
    int dryChannels = 0, dryFrames = 0;
    float bypassMix = 0.0f, bypassStep = 1.0f;

    bool bypassParameterOn() const;
    /* One block while the fade runs: process(), then the input mixed in. */
    void processFading (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool bypassed);

    /* The state readState refused, kept to be saved back (stateUnread). The
     * state calls and the message thread's edits may come on different
     * threads, never on the audio one, so a lock guards it. */
    juce::MemoryBlock unread;
    bool holdingUnread = false;
    mutable juce::CriticalSection unreadLock;
    void forgetUnread();

    /* A gesture on any parameter is the user editing: what lets go of an
     * unread state. */
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace ni
