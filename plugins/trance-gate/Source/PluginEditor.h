#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PhosphorLookAndFeel.h"
#include "PhosphorComponents.h"

/*
 * The editor draws from the SAME `ui` readout the Move display uses:
 *
 *     steps : ties : length : phase : ms_step : advancing : cursor : depths
 *
 * so the picture here and the ring on the hardware are driven by one fact,
 * not two. The playhead is extrapolated between reads exactly as ui_chain.js
 * does -- the readout is a snapshot, and differencing it per frame would show
 * the rotation's stutter rather than the music.
 *
 * THE CONTROLS ARE ATTACHED TO PARAMETERS, NOT TO THE ENGINE. Everything that
 * is a parameter goes through the APVTS and the engine is written by the
 * processor's listener, so a knob turned here shows up in Live's automation
 * lane. The PATTERN is not a parameter (128 steps x 8 slots is not an
 * automation lane anyone wants), so the grid still talks to the engine
 * directly. That asymmetry is the design, not an oversight.
 *
 * THE LOOK IS THE PHOSPHOR DESIGN SYSTEM, and this file holds none of it:
 * colours and metrics live in Phosphor.h, control drawing in
 * PhosphorLookAndFeel, and the ring, grid, panels and hint bar in
 * PhosphorComponents. What is left here is layout and wiring.
 */
class TranceGateEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit TranceGateEditor (TranceGateProcessor&);
    ~TranceGateEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /* 128 steps do not fit a machine word any more. Same shape as the
     * engine's tg_mask_t, and for the same reason it is a struct there: a
     * bare uint32_t still compiles with `>> 40` and quietly drops the steps
     * past 31. */
    struct Mask
    {
        uint32_t w[4] {};
        bool get (int i) const
        {
            return i >= 0 && i < 128 && ((w[i >> 5] >> (i & 31)) & 1u) != 0u;
        }
        /*
         * The engine writes the HIGHEST non-zero word first, unpadded, then
         * each lower word as exactly 8 digits -- so a <=32-step pattern is
         * still the same 8-digit string it always was. Parsing therefore runs
         * from the RIGHT in 8-digit chunks; reading left to right would put
         * word 0 in the wrong place for every pattern longer than 32 steps,
         * and leave short ones looking perfectly correct.
         */
        static Mask fromHex (const juce::String& hex)
        {
            Mask m;
            int end = hex.length();
            for (int k = 0; k < 4 && end > 0; ++k)
            {
                const int start = juce::jmax (0, end - 8);
                m.w[k] = (uint32_t) hex.substring (start, end).getHexValue64();
                end = start;
            }
            return m;
        }
    };

    /* The window is 824 wide: space-8 either side of the system's 760px
     * 16-step grid. Its height follows the pattern -- see heightFor. */
    static constexpr int windowWidth = phosphor::space::s8 * 2 + StepGridView::width;
    static int heightFor (int length);

    /* Where the pattern plot sits. Exposed so a screenshot can frame it
     * without a second copy of the layout's arithmetic going stale. */
    static int plotStripTop();
    static int plotStripHeight();

private:
    /*
     * The envelope panel: ONE step's envelope, on the envelope's own axis.
     * The SHAPE comes from TgEnvelopeShape, which renders it through the
     * engine; this class only draws, and decides when the shape is stale.
     *
     * THE X-AXIS IS THE WHOLE ENVELOPE -- attack, decay and release as
     * dialled -- with the step edge and the gate drawn on it as rules. It
     * used to be one step wide, on the argument that A, D and R are absolute
     * milliseconds while a step is not, so the same ADSR is a swell at 1/4
     * and is never finished at 1/32. That fact is still the thing worth
     * seeing; clipping the curve at the step was simply the wrong way to show
     * it, because a 442 ms decay on a 29.5 ms step became 6% of a decay and
     * told you nothing about the knob you were turning. The step is now a
     * mark near the left-hand edge instead, which says the same thing and
     * leaves the shape legible.
     */
    class EnvelopeCurve : public juce::Component
    {
    public:
        explicit EnvelopeCurve (TranceGateProcessor&);
        void paint (juce::Graphics&) override;
        /* Cheap to call every frame: it re-renders only when the macros that
         * shape the curve have actually moved. */
        void refresh (double msStep);

    private:
        void rebuild (int cols);

        TranceGateProcessor& proc;
        juce::String stamp;                 /* the macros this curve was built from */
        TgEnvelopeShape shape;
        phosphor::plot::Curve curve;        /* the real gated envelope */
        phosphor::plot::Curve ghost;        /* the shape as dialled, behind it */
        std::vector<float> lo, hi;
        int builtFor = 0;                   /* the width `curve` was built for */
    };

    /*
     * The pattern panel: the gate across EVERY step, which is the thing a
     * one-step preview cannot show -- the retrigger that cuts a long decay at
     * the next step's attack, the tie that suppresses it, legato, and each
     * step's own amount.
     *
     * It draws the engine's real output gain, so the only way for it to
     * disagree with what you hear is for the engine to disagree with itself.
     * The global Amount is the exception, and deliberately so: it is an
     * affine floor applied in the paint transform, so dragging it repaints
     * without re-rendering.
     */
    class PatternCurve : public juce::Component
    {
    public:
        explicit PatternCurve (TranceGateProcessor&);
        void paint (juce::Graphics&) override;
        /* `uiRaw` is the engine's readout with the volatile fields still in
         * it; refresh drops them, so a moving playhead is not a re-render. */
        void refresh (const juce::String& uiRaw, double msStep);
        void setPlayhead (double stepPhase, bool moving);
        /*
         * AMOUNT REPAINTS BUT NEVER RE-RENDERS.
         *
         * It is an affine floor applied in the paint transform, so it is
         * deliberately absent from the stamp -- but that left nothing at all
         * to trigger a repaint, and the floor simply did not move until
         * something else did. With the transport running the playhead
         * repainted every frame and hid it, which is what made it look
         * intermittent rather than broken.
         */
        void setAmount (float a);

    private:
        void rebuild (int cols);

        TranceGateProcessor& proc;
        juce::String stamp;
        TgPatternShape shape;
        phosphor::plot::Curve curve;
        std::vector<float> lo, hi;
        int    builtFor = 0;
        double phase    = 0.0;
        bool   moving   = false;
        float  amount   = 1.0f;
    };

    struct Ui
    {
        bool   valid   = false;
        Mask   steps, ties;
        int    length  = 16;
        double phase   = 0.0;
        double msStep  = 0.0;
        bool   moving  = false;
        int    cursor  = 0;
        float  depth[128] {};
    };

    void timerCallback() override;
    /* The three stage readouts print through the PARAMETER, so a mode change
     * moves no value and JUCE has nothing to notify. They are pushed. */
    void refreshStageText();
    void refreshUi();
    void pushModels();
    double livePhase() const;

    TranceGateProcessor& proc;
    Ui ui;

    PhosphorLookAndFeel phosphorLook;

    /* The local clock the playhead is advanced on between readouts. */
    juce::String lastRaw;
    double anchorPhase = 0.0;
    double anchorMs    = 0.0;
    int    lastRows    = 0;

    juce::Slider rate, length, amount, gate, attack, decay, sustain, release;
    TrackedLabel rateL { "Rate" }, lengthL { "Length" }, amountL { "Amount" },
                 gateL { "Gate" }, attackL { "Attack" }, decayL { "Decay" },
                 sustainL { "Sustain" }, releaseL { "Release" };
    juce::TextButton copyPatch { "Copy patch" }, pastePatch { "Paste patch" };
    juce::ToggleButton legato { "Join Neighbors" };
    juce::ComboBox slot, timeMode;
    TrackedLabel timeModeL { "Env Time" };

    PanelBox gatePanel { "Gate" }, envPanel { "Envelope" };
    RingDisplay  ring;
    StepGridView grid;
    HintBar      hint;
    EnvelopeCurve envelope { proc };
    PatternCurve  pattern  { proc };

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<SliderAtt>> sliderAtts;
    std::unique_ptr<ComboAtt>  slotAtt, timeModeAtt;
    std::unique_ptr<ButtonAtt> legatoAtt;

    /* Knobs are children of their PANEL, not of the editor. Parented to the
     * editor they were painted first and the panel's ground covered them --
     * and the hierarchy would have been lying about which controls belong to
     * which function anyway. */
    void wireKnob (PanelBox&, juce::Slider&, TrackedLabel&, const juce::String& paramId);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranceGateEditor)
};
