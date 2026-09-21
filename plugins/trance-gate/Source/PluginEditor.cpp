#include "PluginEditor.h"

namespace {
constexpr int kW = 720, kH = 420;
constexpr int kGridTop = 200, kStepH = 74, kPad = 16;

const char* kRates[] = { "1/1T","1/2","1/2T","1/4","1/4T","1/8",
                         "1/8T","1/16","1/16T","1/32","1/32T","1/64" };
constexpr int kNumRates = 12;
}

TranceGateEditor::TranceGateEditor (TranceGateProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    wireKnob (amount,  amountL,  "Amount",  "amount",  0.0, 1.0,   0.01, "");
    wireKnob (gate,    gateL,    "Gate",    "hold",    0.05, 1.0,  0.01, "");
    wireKnob (attack,  attackL,  "Attack",  "attack",  0.0, 500.0, 1.0,  " ms");
    wireKnob (decay,   decayL,   "Decay",   "decay",   0.0, 500.0, 1.0,  " ms");
    wireKnob (sustain, sustainL, "Sustain", "sustain", 0.0, 1.0,   0.01, "");
    wireKnob (release, releaseL, "Release", "release", 0.0, 500.0, 1.0,  " ms");

    /* Length is an enum on the wire -- the INDEX, where index 15 is "16
     * steps". Sending the displayed number would set 17. */
    length.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    length.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    length.setRange (1.0, 32.0, 1.0);
    length.onValueChange = [this]
    {
        proc.engineSet ("length", juce::String ((int) length.getValue() - 1));
        refreshUi();
        repaint();
    };
    addAndMakeVisible (length);
    lengthL.setText ("Length", juce::dontSendNotification);
    lengthL.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (lengthL);

    for (int i = 0; i < kNumRates; ++i) rate.setName ({});
    rate.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    rate.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    rate.setRange (0.0, kNumRates - 1.0, 1.0);
    rate.onValueChange = [this]
    {
        const int i = juce::jlimit (0, kNumRates - 1, (int) rate.getValue());
        /* Rate is name-wired: its options are labels, not numerals, so the
         * label IS the wire value. */
        proc.engineSet ("rate", kRates[i]);
        rateL.setText (juce::String ("Rate  ") + kRates[i], juce::dontSendNotification);
        refreshUi();
        repaint();
    };
    addAndMakeVisible (rate);
    rateL.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (rateL);

    slot.addItemList ({ "1","2","3","4","5","6","7","8" }, 1);
    slot.onChange = [this]
    {
        /* Slot is index-wired and declares it, so 0 means pattern 1. */
        proc.engineSet ("slot", juce::String (slot.getSelectedItemIndex()));
        refreshUi();
        repaint();
    };
    addAndMakeVisible (slot);

    copyPatch.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (proc.patchToString());
    };
    pastePatch.onClick = [this]
    {
        if (proc.patchFromString (juce::SystemClipboard::getTextFromClipboard()))
        {
            refreshUi();
            repaint();
        }
    };
    addAndMakeVisible (copyPatch);
    addAndMakeVisible (pastePatch);

    refreshUi();
    /* Pull the macros up from the engine so the editor opens showing the
     * patch rather than its own defaults. */
    amount .setValue (proc.engineGet ("amount") .getDoubleValue(), juce::dontSendNotification);
    gate   .setValue (proc.engineGet ("hold")   .getDoubleValue(), juce::dontSendNotification);
    attack .setValue (proc.engineGet ("attack") .getDoubleValue(), juce::dontSendNotification);
    decay  .setValue (proc.engineGet ("decay")  .getDoubleValue(), juce::dontSendNotification);
    sustain.setValue (proc.engineGet ("sustain").getDoubleValue(), juce::dontSendNotification);
    release.setValue (proc.engineGet ("release").getDoubleValue(), juce::dontSendNotification);
    length .setValue (ui.length, juce::dontSendNotification);
    {
        const auto r = proc.engineGet ("rate");
        for (int i = 0; i < kNumRates; ++i)
            if (r == kRates[i]) rate.setValue (i, juce::dontSendNotification);
        rateL.setText (juce::String ("Rate  ") + r, juce::dontSendNotification);
    }
    slot.setSelectedItemIndex (proc.engineGet ("slot").getIntValue(), juce::dontSendNotification);

    setSize (kW, kH);
    startTimerHz (30);
}

TranceGateEditor::~TranceGateEditor() { stopTimer(); }

void TranceGateEditor::wireKnob (juce::Slider& s, juce::Label& l, const juce::String& text,
                                 const juce::String& key, double lo, double hi, double step,
                                 const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    s.setRange (lo, hi, step);
    s.setTextValueSuffix (suffix);
    s.onValueChange = [this, &s, key] { proc.engineSet (key, juce::String (s.getValue(), 3)); };
    addAndMakeVisible (s);
    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (l);
}

/*
 * The compound readout, parsed exactly as ui_chain.js parses it. One string,
 * one rotation stop -- reading eight keys separately would cost eight.
 */
void TranceGateEditor::refreshUi()
{
    const auto raw = proc.engineGet ("ui");
    if (raw.isEmpty()) { ui.valid = false; return; }

    auto parts = juce::StringArray::fromTokens (raw, ":", "");
    if (parts.size() < 7) { ui.valid = false; return; }

    Ui u;
    u.steps  = (uint32_t) parts[0].getHexValue64();
    u.ties   = (uint32_t) parts[1].getHexValue64();
    u.length = juce::jlimit (1, 32, parts[2].getIntValue());
    u.phase  = parts[3].getDoubleValue();
    u.msStep = parts[4].getDoubleValue();
    u.moving = parts[5] == "1";
    u.cursor = parts[6].getIntValue();

    /* Per-step depths, two hex digits each. ABSENT MEANS FULL, never zero --
     * a missing read must not draw a ring of silent steps. */
    const auto d = parts.size() > 7 ? parts[7] : juce::String();
    for (int i = 0; i < 32; ++i)
    {
        const auto pair = d.substring (i * 2, i * 2 + 2);
        u.depth[i] = pair.length() == 2 ? (float) pair.getHexValue32() / 255.0f : 1.0f;
    }
    u.valid = true;
    ui = u;

    /* Re-anchor only on a FRESH reading. The readout is a snapshot; taking it
     * as the playhead every frame shows the read rotation, not the music. */
    if (raw != lastRaw)
    {
        lastRaw = raw;
        anchorPhase = ui.phase;
        anchorMs    = juce::Time::getMillisecondCounterHiRes();
    }
}

double TranceGateEditor::livePhase() const
{
    if (! ui.valid || ! ui.moving || ui.msStep <= 0.0) return anchorPhase;
    const double elapsed = juce::Time::getMillisecondCounterHiRes() - anchorMs;
    double ph = anchorPhase + elapsed / ui.msStep;
    ph = std::fmod (ph, (double) ui.length);
    return ph < 0.0 ? ph + ui.length : ph;
}

void TranceGateEditor::timerCallback() { refreshUi(); repaint(); }

juce::Rectangle<int> TranceGateEditor::stepBounds (int i) const
{
    const int cols = 16;
    const int w = (kW - 2 * kPad) / cols;
    const int row = i / cols, col = i % cols;
    return { kPad + col * w, kGridTop + row * (kStepH / 2), w - 3, kStepH / 2 - 4 };
}

int TranceGateEditor::stepAt (juce::Point<int> pt) const
{
    for (int i = 0; i < ui.length; ++i)
        if (stepBounds (i).contains (pt)) return i;
    return -1;
}

void TranceGateEditor::mouseDown (const juce::MouseEvent& e)
{
    const int i = stepAt (e.getPosition());
    if (i < 0) return;

    proc.engineSet ("cursor", juce::String (i));
    const bool on  = (ui.steps >> i) & 1u;
    const bool tie = (ui.ties  >> i) & 1u;

    /* Plain click is on/off; shift is the tie -- the same split the pads use,
     * and for the same reason: drawing a pattern is the frequent gesture and
     * must not cycle through a third state to get back to off. */
    if (e.mods.isShiftDown()) proc.engineSet ("step", on ? (tie ? "On" : "Tie") : "On");
    else                      proc.engineSet ("step", on ? "Off" : "On");

    refreshUi();
    repaint();
}

void TranceGateEditor::mouseDrag (const juce::MouseEvent& e)
{
    /* Vertical drag on a step sets that step's amount -- the accent. */
    const int i = stepAt (e.getMouseDownPosition());
    if (i < 0) return;
    const auto b = stepBounds (i);
    const float v = juce::jlimit (0.0f, 1.0f,
        1.0f - (float) (e.position.y - b.getY()) / (float) b.getHeight());
    proc.engineSet ("cursor", juce::String (i));
    proc.engineSet ("step_amount", juce::String (v, 3));
    refreshUi();
    repaint();
}

void TranceGateEditor::drawRing (juce::Graphics& g, juce::Rectangle<float> r) const
{
    const auto c = r.getCentre();
    const float outer = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 4.0f;
    const int n = ui.length;
    const float sweep = juce::MathConstants<float>::twoPi / (float) n;

    for (int i = 0; i < n; ++i)
    {
        const bool on = (ui.steps >> i) & 1u;
        const float a0 = -juce::MathConstants<float>::halfPi + i * sweep + sweep * 0.08f;
        const float a1 = a0 + sweep * 0.84f;
        /* Thickness is the step's amount, as on the hardware ring. */
        const float thick = on ? juce::jmap (ui.depth[i], 0.0f, 1.0f, 3.0f, 13.0f) : 2.0f;

        juce::Path p;
        p.addCentredArc (c.x, c.y, outer - thick * 0.5f, outer - thick * 0.5f,
                         0.0f, a0, a1, true);
        g.setColour (on ? juce::Colour (0xff35d07f) : juce::Colour (0xff4a2530));
        g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::butt));
    }

    const int head = (int) std::floor (livePhase());
    if (ui.moving && head >= 0 && head < n)
    {
        const float a = -juce::MathConstants<float>::halfPi + (head + 0.5f) * sweep;
        g.setColour (juce::Colours::white);
        g.fillEllipse (c.x + std::cos (a) * (outer - 22.0f) - 4.0f,
                       c.y + std::sin (a) * (outer - 22.0f) - 4.0f, 8.0f, 8.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (juce::FontOptions (22.0f));
    g.drawText (juce::String (ui.cursor + 1), r, juce::Justification::centred);
}

void TranceGateEditor::drawSteps (juce::Graphics& g) const
{
    for (int i = 0; i < ui.length; ++i)
    {
        const auto b = stepBounds (i).toFloat();
        const bool on  = (ui.steps >> i) & 1u;
        const bool tie = (ui.ties  >> i) & 1u;

        g.setColour (juce::Colour (0xff1b1b1f));
        g.fillRoundedRectangle (b, 3.0f);

        if (on)
        {
            /* Height is the step's amount: the same fact the pad brightness
             * carries on Move, in the dimension this surface actually has. */
            const float h = juce::jmax (3.0f, b.getHeight() * ui.depth[i]);
            g.setColour (juce::Colour (0xff35d07f));
            g.fillRoundedRectangle (b.withTop (b.getBottom() - h), 3.0f);
            if (tie)
            {
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                g.fillRect (b.getRight() - 2.0f, b.getY() + 2.0f, 4.0f, b.getHeight() - 4.0f);
            }
        }

        if (i == ui.cursor)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawRoundedRectangle (b, 3.0f, 1.5f);
        }

        const int head = (int) std::floor (livePhase());
        if (ui.moving && i == head)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRoundedRectangle (b, 3.0f);
        }
    }
}

void TranceGateEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff121215));

    if (! ui.valid)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (16.0f));
        g.drawText ("...", getLocalBounds(), juce::Justification::centred);
        return;
    }

    drawRing (g, juce::Rectangle<float> (kPad, 10.0f, 170.0f, 170.0f));
    drawSteps (g);

    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("click a step to toggle  -  shift-click for a tie  -  drag up/down for its amount",
                0, kH - 26, kW, 20, juce::Justification::centred);
}

void TranceGateEditor::resized()
{
    int x = 210, y = 16;
    auto place = [&] (juce::Slider& s, juce::Label& l)
    {
        l.setBounds (x, y, 74, 16);
        s.setBounds (x, y + 16, 74, 74);
        x += 80;
    };
    place (rate, rateL);
    place (length, lengthL);
    place (amount, amountL);
    place (gate, gateL);
    x = 210; y = 110;
    place (attack, attackL);
    place (decay, decayL);
    place (sustain, sustainL);
    place (release, releaseL);

    slot.setBounds (kPad, 186, 80, 22);
    copyPatch .setBounds (kW - 210, 160, 95, 24);
    pastePatch.setBounds (kW - 108, 160, 95, 24);
}
