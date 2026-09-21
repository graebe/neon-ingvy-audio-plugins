#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/*
 * The editor draws from the SAME `ui` readout the Move display uses:
 *
 *     steps : ties : length : phase : ms_step : advancing : cursor : depths
 *
 * so the picture here and the ring on the hardware are driven by one fact,
 * not two. The playhead is extrapolated between reads exactly as ui_chain.js
 * does -- the readout is a snapshot, and differencing it per frame would show
 * the rotation's stutter rather than the music.
 */
class TranceGateEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit TranceGateEditor (TranceGateProcessor&);
    ~TranceGateEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    struct Ui
    {
        bool     valid   = false;
        uint32_t steps   = 0;
        uint32_t ties    = 0;
        int      length  = 16;
        double   phase   = 0.0;
        double   msStep  = 0.0;
        bool     moving  = false;
        int      cursor  = 0;
        float    depth[32] {};
    };

    void timerCallback() override;
    void refreshUi();
    double livePhase() const;
    int    stepAt (juce::Point<int>) const;
    juce::Rectangle<int> stepBounds (int i) const;
    void   drawRing (juce::Graphics&, juce::Rectangle<float>) const;
    void   drawSteps (juce::Graphics&) const;

    TranceGateProcessor& proc;
    Ui ui;

    /* The local clock the playhead is advanced on between readouts. */
    juce::String lastRaw;
    double anchorPhase = 0.0;
    double anchorMs    = 0.0;

    juce::Slider rate, length, amount, gate, attack, decay, sustain, release;
    juce::Label  rateL, lengthL, amountL, gateL, attackL, decayL, sustainL, releaseL;
    juce::TextButton copyPatch { "Copy patch" }, pastePatch { "Paste patch" };
    juce::ComboBox slot;

    void wireKnob (juce::Slider&, juce::Label&, const juce::String& text,
                   const juce::String& key, double lo, double hi, double step,
                   const juce::String& suffix);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranceGateEditor)
};
