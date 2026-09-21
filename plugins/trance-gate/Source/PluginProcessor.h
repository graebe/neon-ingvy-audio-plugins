#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
extern "C" {
#include "trance_gate_core.h"
}

/*
 * The plugin shell. Schwung's shell is trance_gate.c; this is its opposite
 * number, and between them sits exactly one engine.
 *
 * NO AUDIOPROCESSORPARAMETERS, deliberately. Pattern, per-step amounts, all
 * eight slots and the macros live in the engine's own state and are edited in
 * our editor -- the same model the Move module uses, where the DSP owns the
 * state and the UI edits it through set_param. Exposing steps as parameters
 * means 32 x 8 = 520 of them, or 64 that get silently rewritten every time you
 * change slot. That is a decision to make with the thing in front of you.
 */
class TranceGateProcessor : public juce::AudioProcessor
{
public:
    TranceGateProcessor();
    ~TranceGateProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Trance Gate"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /* The editor talks to the engine the way Schwung's UI does: strings in,
     * strings out. One parameter surface, so a control cannot behave
     * differently here than it does on the hardware. */
    void        engineSet (const juce::String& key, const juce::String& value);
    juce::String engineGet (const juce::String& key) const;

    /* The patch, as the Move module writes it. This is the interchange
     * format -- paste one in, or copy one out. */
    juce::String patchToString() const  { return engineGet ("state"); }
    bool         patchFromString (const juce::String& s);

private:
    tg_core_t* core = nullptr;
    /* set_param on the audio thread is what Schwung does too, but there the
     * caller IS the audio thread. Here the editor is not, so writes are
     * serialised against the block. */
    mutable juce::CriticalSection engineLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranceGateProcessor)
};
