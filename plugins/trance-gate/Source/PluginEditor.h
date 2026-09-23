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

private:
    /*
     * The envelope panel. The SHAPE comes from TgEnvelopeShape, which renders
     * it through the engine; this class only draws, and decides when the
     * shape is stale.
     *
     * The x-axis is ONE STEP at the current rate and tempo, which is the whole
     * point of drawing it at all. Attack, Decay and Release are absolute
     * milliseconds while the step is not: the same ADSR is a gentle swell at
     * 1/4 and is never finished at 1/32. A curve on a normalised axis would
     * hide exactly that; this one runs off the right-hand edge, which is the
     * truth and is the thing worth seeing.
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
        TranceGateProcessor& proc;
        juce::String stamp;                 /* the macros this curve was built from */
        TgEnvelopeShape shape;
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
    juce::ToggleButton legato { "Legato" };
    juce::ComboBox slot;

    PanelBox gatePanel { "Gate" }, envPanel { "Envelope" };
    RingDisplay  ring;
    StepGridView grid;
    HintBar      hint;
    EnvelopeCurve envelope { proc };

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<SliderAtt>> sliderAtts;
    std::unique_ptr<ComboAtt>  slotAtt;
    std::unique_ptr<ButtonAtt> legatoAtt;

    /* Knobs are children of their PANEL, not of the editor. Parented to the
     * editor they were painted first and the panel's ground covered them --
     * and the hierarchy would have been lying about which controls belong to
     * which function anyway. */
    void wireKnob (PanelBox&, juce::Slider&, TrackedLabel&, const juce::String& paramId);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranceGateEditor)
};
