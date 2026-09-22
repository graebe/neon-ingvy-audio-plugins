#include "PluginEditor.h"

namespace {
constexpr int kW = 820, kH = 520;
constexpr int kPad = 16;
constexpr int kGridTop = 228, kGridBot = kH - 34;
constexpr int kCols = 16;           /* one row is one bar at 1/16 */
constexpr int kMaxSteps = 128;

const char* kRates[] = { "1/1T","1/2","1/2T","1/4","1/4T","1/8","1/8T",
                         "1/16","1/16T","1/32","1/32T","1/64","1/128" };
constexpr int kNumRates = 13;

const juce::Colour kBg    { 0xff121215 };
const juce::Colour kCell  { 0xff1b1b1f };
const juce::Colour kOn    { 0xff35d07f };
const juce::Colour kOff   { 0xff4a2530 };
const juce::Colour kPanel { 0xff17171c };
}

/* ======================================================== envelope curve == */

TranceGateEditor::EnvelopeCurve::EnvelopeCurve (TranceGateProcessor& p) : proc (p) {}

void TranceGateEditor::EnvelopeCurve::refresh (double stepMs)
{
    /* Everything that changes the shape, and nothing that does not: the
     * playhead moves 30 times a second and must not cause a re-render. */
    const juce::String now = proc.engineGet ("attack") + "/" + proc.engineGet ("decay")
                           + "/" + proc.engineGet ("sustain") + "/" + proc.engineGet ("release")
                           + "/" + proc.engineGet ("hold") + "/" + juce::String (stepMs, 2);
    if (now == stamp) return;
    stamp = now;
    shape = TgEnvelopeShape::render (proc, stepMs);
    repaint();
}

void TranceGateEditor::EnvelopeCurve::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (kPanel);
    g.fillRoundedRectangle (r, 4.0f);

    auto plot = r.reduced (10.0f, 12.0f).withTrimmedTop (12.0f);

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (shape.msStep > 0.0 ? "Envelope  -  one step = " + juce::String (shape.msStep, 1) + " ms"
                             : "Envelope",
                (int) r.getX() + 10, (int) r.getY() + 5, (int) r.getWidth() - 20, 14,
                juce::Justification::centredLeft);

    /* The baseline and the top, so the curve has something to be read against. */
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawHorizontalLine ((int) plot.getBottom(), plot.getX(), plot.getRight());
    g.drawHorizontalLine ((int) plot.getY(),      plot.getX(), plot.getRight());

    if (shape.env.empty())
    {
        g.setColour (juce::Colours::grey);
        g.drawText ("...", getLocalBounds(), juce::Justification::centred);
        return;
    }

    /* Where the gate closes. Drawn BEFORE the curve so the curve sits on top,
     * and labelled, because "Gate" is otherwise a number you learn by ear. */
    if (shape.holdFrac < 1.0)
    {
        const float x = plot.getX() + plot.getWidth() * (float) shape.holdFrac;
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("gate", (int) x + 3, (int) plot.getY(), 34, 12,
                    juce::Justification::centredLeft);
    }

    juce::Path p;
    const int n = (int) shape.env.size();
    for (int i = 0; i < n; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * ((float) i / (float) (n - 1));
        const float y = plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, shape.env[(size_t) i]);
        if (i == 0) p.startNewSubPath (x, y);
        else        p.lineTo (x, y);
    }

    auto fill = p;
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.lineTo (plot.getX(), plot.getBottom());
    fill.closeSubPath();
    g.setColour (kOn.withAlpha (0.16f));
    g.fillPath (fill);

    g.setColour (kOn);
    g.strokePath (p, juce::PathStrokeType (1.8f));

    /* A release that cannot finish inside the step is CUT OFF at the edge --
     * say so, rather than leaving it to look like a drawing bug. */
    if (shape.truncated)
    {
        g.setColour (juce::Colours::orange.withAlpha (0.9f));
        g.drawVerticalLine ((int) plot.getRight() - 1, plot.getY(), plot.getBottom());
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("cut off", (int) plot.getRight() - 52, (int) plot.getBottom() - 13, 50, 12,
                    juce::Justification::centredRight);
    }
}

/* ================================================================ editor == */

TranceGateEditor::TranceGateEditor (TranceGateProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    wireKnob (amount,  amountL,  "Amount",  "amount",  "");
    wireKnob (gate,    gateL,    "Gate",    "hold",    "");
    wireKnob (attack,  attackL,  "Attack",  "attack",  " ms");
    wireKnob (decay,   decayL,   "Decay",   "decay",   " ms");
    wireKnob (sustain, sustainL, "Sustain", "sustain", "");
    wireKnob (release, releaseL, "Release", "release", " ms");
    wireKnob (length,  lengthL,  "Length",  "length",  "");

    /* Rate shows its LABEL, not its index -- the number would be meaningless.
     * The attachment still owns the value; this only follows it. */
    rate.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    rate.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    rate.onValueChange = [this]
    {
        const int i = juce::jlimit (0, kNumRates - 1, (int) rate.getValue());
        rateL.setText (juce::String ("Rate  ") + kRates[i], juce::dontSendNotification);
    };
    addAndMakeVisible (rate);
    rateL.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (rateL);
    sliderAtts.push_back (std::make_unique<SliderAtt> (proc.state(), "rate", rate));
    rate.onValueChange();

    slot.addItemList ({ "1","2","3","4","5","6","7","8" }, 1);
    addAndMakeVisible (slot);
    slotAtt = std::make_unique<ComboAtt> (proc.state(), "slot", slot);

    legato.setTooltip ("Adjacent open steps hold as one gate instead of re-articulating");
    addAndMakeVisible (legato);
    legatoAtt = std::make_unique<ButtonAtt> (proc.state(), "legato", legato);

    copyPatch.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (proc.patchToString());
    };
    pastePatch.onClick = [this]
    {
        if (proc.patchFromString (juce::SystemClipboard::getTextFromClipboard()))
        {
            /* The patch is authoritative at load, so the parameters -- and
             * therefore every attached control -- follow it. */
            proc.syncParamsFromEngine();
            refreshUi();
            repaint();
        }
    };
    addAndMakeVisible (copyPatch);
    addAndMakeVisible (pastePatch);
    addAndMakeVisible (envelope);

    refreshUi();
    setSize (kW, kH);
    startTimerHz (30);
}

TranceGateEditor::~TranceGateEditor() { stopTimer(); }

void TranceGateEditor::wireKnob (juce::Slider& s, juce::Label& l, const juce::String& text,
                                 const juce::String& paramId, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    s.setTextValueSuffix (suffix);
    addAndMakeVisible (s);
    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (l);
    /* The attachment sets the range, the value and the two-way link. Setting
     * a range by hand here would be a second opinion about the parameter. */
    sliderAtts.push_back (std::make_unique<SliderAtt> (proc.state(), paramId, s));
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
    u.steps  = Mask::fromHex (parts[0]);
    u.ties   = Mask::fromHex (parts[1]);
    u.length = juce::jlimit (1, kMaxSteps, parts[2].getIntValue());
    u.phase  = parts[3].getDoubleValue();
    u.msStep = parts[4].getDoubleValue();
    u.moving = parts[5] == "1";
    u.cursor = juce::jlimit (0, kMaxSteps - 1, parts[6].getIntValue());

    /* Per-step depths, two hex digits each, for `length` steps. ABSENT MEANS
     * FULL, never zero -- a short read must not draw a ring of silent steps. */
    const auto d = parts.size() > 7 ? parts[7] : juce::String();
    for (int i = 0; i < kMaxSteps; ++i)
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

void TranceGateEditor::timerCallback()
{
    const int rowsBefore = gridRows();
    refreshUi();
    envelope.refresh (ui.msStep);
    if (gridRows() != rowsBefore) resized();
    repaint();
}

int TranceGateEditor::gridRows() const
{
    return juce::jmax (1, (ui.length + kCols - 1) / kCols);
}

/*
 * THE GRID GROWS ROWS, IT DOES NOT SCROLL.
 *
 * 16 columns is not an arbitrary fit: at 1/16 one row is one bar, so a
 * 64-step pattern reads as four bars stacked. A 16-step pattern keeps tall
 * cells and the full 128 shrinks them rather than hiding any -- the pattern
 * is the thing being edited, and a step you cannot see is a step you cannot
 * fix. (Move pages 32 at a time because it has 32 pads; this surface has no
 * such limit and should not invent one.)
 */
juce::Rectangle<int> TranceGateEditor::stepBounds (int i) const
{
    const int rows  = gridRows();
    const int cellH = juce::jlimit (14, 48, (kGridBot - kGridTop) / rows);
    const int w     = (kW - 2 * kPad) / kCols;
    const int row = i / kCols, col = i % kCols;
    return { kPad + col * w, kGridTop + row * cellH, w - 3, cellH - 4 };
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
    const bool on  = ui.steps.get (i);
    const bool tie = ui.ties .get (i);

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
        1.0f - (e.position.y - (float) b.getY()) / (float) b.getHeight());
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
    /* The gap between segments is a FRACTION of the sweep, so 128 steps do
     * not close up into a solid disc. */
    const float gap = juce::jmin (0.16f, 0.08f + 2.0f / (float) n);

    for (int i = 0; i < n; ++i)
    {
        const bool on = ui.steps.get (i);
        const float a0 = -juce::MathConstants<float>::halfPi + (float) i * sweep + sweep * gap * 0.5f;
        const float a1 = a0 + sweep * (1.0f - gap);
        /* Thickness is the step's amount, as on the hardware ring -- scaled
         * down when the segments get thin, or they overlap. */
        const float maxT = juce::jmin (13.0f, 4.0f + 160.0f / (float) n);
        const float thick = on ? juce::jmap (ui.depth[i], 0.0f, 1.0f, 3.0f, maxT) : 2.0f;

        juce::Path p;
        p.addCentredArc (c.x, c.y, outer - thick * 0.5f, outer - thick * 0.5f,
                         0.0f, a0, a1, true);
        g.setColour (on ? kOn : kOff);
        g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::butt));
    }

    const int head = (int) std::floor (livePhase());
    if (ui.moving && head >= 0 && head < n)
    {
        const float a = -juce::MathConstants<float>::halfPi + ((float) head + 0.5f) * sweep;
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
    const int head = (int) std::floor (livePhase());

    for (int i = 0; i < ui.length; ++i)
    {
        const auto b = stepBounds (i).toFloat();
        const bool on  = ui.steps.get (i);
        const bool tie = ui.ties .get (i);

        /* Every fourth step a shade lighter: at 1/16 that is the beat, and
         * without it a 128-step row is 16 identical boxes to count along. */
        g.setColour (i % 4 == 0 ? kCell.brighter (0.22f) : kCell);
        g.fillRoundedRectangle (b, 3.0f);

        if (on)
        {
            /* Height is the step's amount: the same fact the pad brightness
             * carries on Move, in the dimension this surface actually has. */
            const float h = juce::jmax (3.0f, b.getHeight() * ui.depth[i]);
            g.setColour (kOn);
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

        if (ui.moving && i == head)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRoundedRectangle (b, 3.0f);
        }
    }
}

void TranceGateEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

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
    int x = 200, y = 16;
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
    x = 200; y = 110;
    place (attack, attackL);
    place (decay, decayL);
    place (sustain, sustainL);
    place (release, releaseL);

    envelope.setBounds (536, 12, kW - 536 - kPad, 172);

    slot      .setBounds (kPad, 192, 80, 24);
    legato    .setBounds (108, 192, 96, 24);
    copyPatch .setBounds (kW - 212, 192, 96, 24);
    pastePatch.setBounds (kW - 108, 192, 96, 24);
}
