#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
extern "C" {
#include "trance_gate_core.h"
}

/*
 * The plugin shell. Schwung's shell is trance_gate.c; this is its opposite
 * number, and between them sits exactly one engine.
 *
 * THE MACROS ARE HOST PARAMETERS; THE PATTERN IS NOT. Rate, Length, Amount,
 * Gate, the envelope, Slot and Legato automate. The 128 steps and their
 * per-step amounts stay in the engine's state, because exposing them is
 * 128 x 8 = 1024 parameters -- or 256 that get silently rewritten every time
 * the Slot parameter moves, which is worse than not having them.
 *
 * ONE RULE KEEPS THE TWO COHERENT: parameters are authoritative at RUNTIME,
 * the patch is authoritative at LOAD. A parameter change writes through to
 * the engine; loading a patch writes the engine and then pushes the macros
 * back OUT into the parameters. Skip that second half and Live's automation
 * lane shows the old value while the engine has the new one -- the sort of
 * disagreement that is invisible until someone automates the control.
 */
class TranceGateProcessor;

/*
 * THE ENVELOPE, RENDERED BY THE ENGINE ITSELF.
 *
 * Not a second implementation of the ADSR maths: this feeds DC through a
 * private engine instance for the WHOLE envelope -- attack, decay and release
 * as dialled, however far past the end of a step they run -- and keeps what
 * comes out. With amount 1 and a full-depth step the
 * engine's output gain reduces to `env` exactly, so the samples ARE the
 * envelope -- a picture derived independently is a picture that can drift
 * from the sound.
 *
 * It lives here rather than in the editor because it is engine work, and
 * because a shape the tests can build without opening a window is a shape the
 * tests can check.
 */
struct TgEnvelopeShape
{
    std::vector<float> env;          /* the REAL gated envelope, per sample */
    /*
     * THE SHAPE AS DIALLED, IGNORING THE GATE -- drawn as a dim ghost behind
     * the real one.
     *
     * Without it the plot could not show Gate at all. The axis is
     * max(gateMs, A+D) + R, so whenever attack and decay outlast the gate
     * point the axis does not move with Gate -- and if the curve is the
     * dialled shape too, then NOTHING moves and the knob looks dead. Drawing
     * both puts the answer on the screen: the solid line is what you hear and
     * follows the knob, the ghost is the decay you set and stays put.
     *
     * Empty when the two coincide, which is whenever the gate closes after
     * the decay has finished -- the ordinary case, and one render not two.
     */
    std::vector<float> envDialled;
    double msStep    = 0.0;          /* what one step lasts */
    double spanMs    = 0.0;          /* what the x-axis covers */
    double gateMs      = 0.0;        /* where the gate REALLY closes */
    double releaseAtMs = 0.0;        /* where the DRAWN release begins */
    double attackMs  = 0.0;
    double decayMs   = 0.0;
    double releaseMs = 0.0;
    double sustainLevel = 1.0;       /* where decay lands, for its marker */
    double holdFrac  = 1.0;          /* where in the STEP the gate closes */
    bool   truncated = false;        /* the release ran past the step's end */
    bool   clamped   = false;        /* the time base could not be honoured */

    /* Positions on the axis, 0..1, which is what a plot needs and what a test
     * can assert without knowing how many samples were rendered. */
    double gateFrac() const { return spanMs > 0.0 ? gateMs / spanMs : 0.0; }
    double releaseFrac() const { return spanMs > 0.0 ? releaseAtMs / spanMs : 0.0; }
    double stepFrac() const { return spanMs > 0.0 ? juce::jmin (1.0, msStep / spanMs) : 0.0; }
    float  levelAt (double frac) const;

    /*
     * RENDERS THROUGH AN ENGINE OF ITS OWN, created and destroyed here.
     *
     * A reused instance carries envelope state between calls -- env, env_t,
     * the stage, the playhead -- so the curve drawn after a knob move would
     * start from wherever the previous render happened to stop. It looks
     * plausible every time and is a different picture depending on what you
     * touched last. An instance is ~1 KB and this runs only when a macro
     * actually moves, never per frame and never on the audio thread.
     */
    static TgEnvelopeShape render (const TranceGateProcessor& src, double msStep);
};

/*
 * THE WHOLE PATTERN, AS THE GATE ACTUALLY OPENS AND CLOSES.
 *
 * TgEnvelopeShape shows one envelope in isolation -- the shape you dialled.
 * This shows what the engine does with it across every step: the retrigger
 * that cuts a long decay at the next step's attack, the tie that suppresses
 * that retrigger and lets it run on, legato, and each step's own amount.
 * Rendered from the REAL patch, so the only way for it to disagree with the
 * audio is for the engine to disagree with itself.
 *
 * `gain` is env * the per-step level. The GLOBAL amount is deliberately not
 * applied -- see the note on the override in render().
 */
struct TgPatternShape
{
    std::vector<float> gain;
    int    length  = 0;
    int    perStep = 0;              /* gain.size() == length * perStep */
    double msStep  = 0.0;
    bool   clamped = false;

    static TgPatternShape render (const TranceGateProcessor& src, double msStep, int columns);
};

class TranceGateProcessor : public juce::AudioProcessor,
                            private juce::AsyncUpdater
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
    juce::AudioProcessorValueTreeState& state() { return apvts; }
    /* Called after the engine's state changes underneath the parameters --
     * a patch paste, or a slot switch that brings a different pattern. */
    void syncParamsFromEngine();
    juce::String engineGet (const juce::String& key) const;

    /* The patch, as the Move module writes it. This is the interchange
     * format -- paste one in, or copy one out. */
    juce::String patchToString() const  { return engineGet ("state"); }
    bool         patchFromString (const juce::String& s);

private:
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    /* Set while pushing engine values INTO the parameters, so the listener
     * that normally writes them back to the engine stands down. Without it a
     * patch load ping-pongs between the two. */
    std::atomic<bool> suppressParamWrite { false };
    void pullParam (const juce::String& id);
    /* A slot change makes every other parameter stale; the refresh has to
     * reach the message thread to touch the host's automation lanes. */
    void handleAsyncUpdate() override;

    tg_core_t* core = nullptr;
    /* set_param on the audio thread is what Schwung does too, but there the
     * caller IS the audio thread. Here the editor is not, so writes are
     * serialised against the block. */
    mutable juce::CriticalSection engineLock;
    struct ParamBridge; std::unique_ptr<ParamBridge> bridge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranceGateProcessor)
};
