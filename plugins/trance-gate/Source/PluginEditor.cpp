#include "PluginEditor.h"

using namespace phosphor;

namespace {
constexpr int kMaxSteps = 128;

/*
 * THE LAYOUT, on the system's 4px grid.
 *
 * Window padding space-8; the content column is exactly the 760px the 16-step
 * grid takes, which is what fixes the window width. The left column is the
 * window's face (ring, then the envelope plot); the control block sits to its
 * right, two panels grouped by function with the actions stacked on the right
 * edge -- "never among the knobs".
 *
 * Ring 240 + space-6 + plot 92 comes to 356, which is also two 166px panels
 * with space-6 between them, so the two columns end level. That is the only
 * reason the plot is 92 and not a rounder number.
 */
constexpr int kPad        = space::s8;
constexpr int kRing       = 240;
constexpr int kPlotH      = 92;
constexpr int kActionsW   = 96;
constexpr int kTopBlockH  = kRing + space::s6 + kPlotH;          /* 356 */
constexpr int kPanelH     = (kTopBlockH - space::s6) / 2;        /* 166 */

constexpr int kLeftX      = kPad;
constexpr int kBlockX     = kPad + kRing + space::s8;            /* 304 */
constexpr int kBlockW     = StepGridView::width - kRing - space::s8;  /* 488 */
constexpr int kPanelW     = kBlockW - kActionsW - space::s4;     /* 376 */
constexpr int kActionsX   = kBlockX + kPanelW + space::s4;

constexpr int kSelectY    = kPad + kTopBlockH + space::s6;       /* 412 */
constexpr int kGridY      = kSelectY + size::controlH + space::s2;
}

/* ====================================================== envelope curve == */

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
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colour::bg000);
    g.fillRect (r);
    g.setColour (colour::line100);
    g.drawRect (r, stroke::hair);

    /* The caption is the step's real duration, which is the fact that makes
     * the whole picture mean something. `hint` style, because it is a note
     * about the drawing rather than a value. */
    const auto fh = font::hint();
    if (shape.msStep > 0.0)
        drawTracked (g, "ONE STEP " + juce::String (shape.msStep, 1) + " MS", fh,
                     colour::inkMuted, { r.getX() + 6.0f, r.getY() + 12.0f },
                     font::trackHint);

    const auto plot = r.reduced (6.0f).withTrimmedTop (14.0f);

    if (shape.env.empty())
    {
        g.setColour (colour::inkDim);
        g.setFont (fh);
        g.drawText ("...", getLocalBounds(), juce::Justification::centred);
        return;
    }

    /* Where the gate closes: a rail-coloured rule, since it marks a position
     * rather than a value. */
    if (shape.holdFrac < 1.0)
    {
        const float x = plot.getX() + plot.getWidth() * (float) shape.holdFrac;
        g.setColour (colour::line200);
        g.drawLine (x, plot.getY(), x, plot.getBottom(), stroke::hair);
    }

    juce::Path p;
    const int n = (int) shape.env.size();
    for (int i = 0; i < n; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * ((float) i / (float) (n - 1));
        const float y = plot.getBottom()
                      - plot.getHeight() * juce::jlimit (0.0f, 1.0f, shape.env[(size_t) i]);
        if (i == 0) p.startNewSubPath (x, y);
        else        p.lineTo (x, y);
    }

    auto fill = p;
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.lineTo (plot.getX(), plot.getBottom());
    fill.closeSubPath();
    g.setColour (colour::phosphorGlow);
    g.fillPath (fill);

    g.setColour (colour::phosphor);
    g.strokePath (p, juce::PathStrokeType (stroke::rail));

    /*
     * A release that cannot finish inside the step is CUT OFF at the edge.
     * Amber, which the system reserves for "armed / about to clip / not
     * right" -- a release with nowhere to go is exactly that, and it is the
     * one amber mark in the window, which is the limit the system sets.
     */
    if (shape.truncated)
    {
        g.setColour (colour::amber);
        g.drawLine (plot.getRight(), plot.getY(), plot.getRight(), plot.getBottom(), stroke::rail);
    }
}

/* ================================================================ editor == */

int TranceGateEditor::heightFor (int length)
{
    return kGridY + StepGridView::heightFor (length) + space::s6 + HintBar::height;
}

TranceGateEditor::TranceGateEditor (TranceGateProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&phosphorLook);

    addAndMakeVisible (gatePanel);
    addAndMakeVisible (envPanel);

    wireKnob (gatePanel, rate,    rateL,    "rate");
    wireKnob (gatePanel, length,  lengthL,  "length");
    wireKnob (gatePanel, amount,  amountL,  "amount");
    wireKnob (gatePanel, gate,    gateL,    "hold");
    wireKnob (envPanel,  attack,  attackL,  "attack");
    wireKnob (envPanel,  decay,   decayL,   "decay");
    wireKnob (envPanel,  sustain, sustainL, "sustain");
    wireKnob (envPanel,  release, releaseL, "release");
    addAndMakeVisible (ring);
    addAndMakeVisible (envelope);
    addAndMakeVisible (grid);
    addAndMakeVisible (hint);

    /* The pattern Select, which the StepGrid card puts above-left of the
     * grid. On Move this is the slot; here it is the same thing named the way
     * the system names it. */
    slot.addItemList ({ "1","2","3","4","5","6","7","8" }, 1);
    addAndMakeVisible (slot);
    slotAtt = std::make_unique<ComboAtt> (proc.state(), "slot", slot);

    legato.setButtonText ("Legato");
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

    /* Three clauses, verb first: the card's maximum and its pattern. */
    hint.setClauses ({ { "click",       "a step to toggle" },
                       { "shift-click", "for a tie" },
                       { "drag",        "up or down for its amount" } });

    grid.onToggle = [this] (int i, bool shift)
    {
        proc.engineSet ("cursor", juce::String (i));
        const bool on  = ui.steps.get (i);
        const bool tie = ui.ties .get (i);
        /* Plain click is on/off; shift is the tie -- the same split the pads
         * use, and for the same reason: drawing a pattern is the frequent
         * gesture and must not cycle through a third state to get back to
         * off. */
        if (shift) proc.engineSet ("step", on ? (tie ? "On" : "Tie") : "On");
        else       proc.engineSet ("step", on ? "Off" : "On");
        refreshUi();
        pushModels();
    };
    grid.onAmount = [this] (int i, float v)
    {
        proc.engineSet ("cursor", juce::String (i));
        proc.engineSet ("step_amount", juce::String (v, 3));
        refreshUi();
        pushModels();
    };

    refreshUi();
    lastRows = StepGridView::rowsFor (ui.length);
    setSize (windowWidth, heightFor (ui.length));
    startTimerHz (30);
}

TranceGateEditor::~TranceGateEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TranceGateEditor::wireKnob (PanelBox& panel, juce::Slider& s, TrackedLabel& l,
                                 const juce::String& paramId)
{
    /*
     * PARENT FIRST, THEN THE TEXT BOX -- the order is load-bearing.
     *
     * setTextBoxStyle builds the box through whatever LookAndFeel the slider
     * has at that moment, and a slider with no parent has JUCE's default.
     * Nothing rebuilds it when the slider is later added to a parent that
     * does have one (Slider rebuilds on lookAndFeelChanged, which reparenting
     * does not send), so the knob came out styled and its readout came out a
     * plain white JUCE label -- the one control in the window that ignored
     * the design system.
     */
    panel.addAndMakeVisible (s);
    panel.addAndMakeVisible (l);

    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    /* "Never draw a knob without its readout: the arc shows position, the
     * readout shows the number." The box is a PhosphorReadout, supplied by
     * the LookAndFeel, so it prints the unit in ink-muted. */
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, size::controlH);
    s.setDoubleClickReturnValue (true, s.getDoubleClickReturnValue());
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

void TranceGateEditor::pushModels()
{
    const int head = (int) std::floor (livePhase());

    RingDisplay::Model rm;
    rm.length   = ui.length;
    rm.playhead = head;
    rm.moving   = ui.moving;
    rm.stepOn   = [this] (int i) { return ui.steps.get (i); };
    /* The window's one readout-size number. The Ring card has the count in
     * the middle and the knob that changes it elsewhere -- which is exactly
     * this arrangement, not a duplication to avoid. */
    rm.centre = juce::String (ui.length);
    rm.label  = "STEPS";
    ring.setModel (std::move (rm));

    StepGridView::Model gm;
    gm.length     = ui.length;
    gm.cursor     = ui.cursor;
    gm.playhead   = head;
    gm.moving     = ui.moving;
    gm.stepOn     = [this] (int i) { return ui.steps.get (i); };
    gm.stepTied   = [this] (int i) { return ui.ties .get (i); };
    gm.stepAmount = [this] (int i) { return ui.depth[juce::jlimit (0, kMaxSteps - 1, i)]; };
    grid.setModel (std::move (gm));
}

void TranceGateEditor::timerCallback()
{
    refreshUi();
    envelope.refresh (ui.msStep);
    pushModels();

    /*
     * THE WINDOW GROWS, THE STEP DOES NOT.
     *
     * The design system would have a pattern over 32 steps become pages
     * rather than smaller steps, precisely so a control is never scaled to
     * fit. This build keeps every step on screen and honours that rule the
     * other way round: the step stays 40px and the editor gets taller. It
     * happens on the eight occasions the ROW COUNT changes, not on every
     * turn of the Length knob, so it is eight discrete jumps across the
     * parameter's whole range rather than a continuous reflow.
     */
    const int rows = StepGridView::rowsFor (ui.length);
    if (rows != lastRows)
    {
        lastRows = rows;
        setSize (windowWidth, heightFor (ui.length));
    }
}

void TranceGateEditor::paint (juce::Graphics& g)
{
    g.fillAll (colour::bg000);

    if (! ui.valid)
    {
        g.setColour (colour::inkDim);
        g.setFont (font::value());
        g.drawText ("...", getLocalBounds(), juce::Justification::centred);
    }
}

void TranceGateEditor::resized()
{
    ring    .setBounds (kLeftX, kPad, kRing, kRing);
    envelope.setBounds (kLeftX, kPad + kRing + space::s6, kRing, kPlotH);

    gatePanel.setBounds (kBlockX, kPad, kPanelW, kPanelH);
    envPanel .setBounds (kBlockX, kPad + kPanelH + space::s6, kPanelW, kPanelH);

    copyPatch .setBounds (kActionsX, kPad, kActionsW, size::controlH);
    pastePatch.setBounds (kActionsX, kPad + size::controlH + space::s2,
                          kActionsW, size::controlH);

    /* Four knobs to a panel: label space-2 above, knob, readout space-2
     * below -- the stack the Knob card describes, on the 4px grid. */
    auto placeRow = [] (PanelBox& panel,
                        std::initializer_list<std::pair<juce::Slider*, TrackedLabel*>> knobs)
    {
        const auto area = panel.contentArea().getTopLeft();
        const int usable = panel.getWidth() - 2 * space::s4;
        const int n      = (int) knobs.size();
        const int colW   = (usable - (n - 1) * space::s4) / n;

        int i = 0;
        for (auto& k : knobs)
        {
            const int x = area.x + i * (colW + space::s4);
            k.second->setBounds (x, area.y, colW, 14);
            /* The slider spans the whole column so its readout box can be the
             * column's width -- the card's min-width is 64 and a 48px box
             * clips "20.0 ms". The knob itself stays 48 and centres inside
             * it; see drawRotarySlider. */
            k.first ->setBounds (x, area.y + 14 + space::s2,
                                 colW, size::knob + space::s2 + size::controlH);
            ++i;
        }
    };
    placeRow (gatePanel, { { &rate, &rateL }, { &length, &lengthL },
                           { &amount, &amountL }, { &gate, &gateL } });
    placeRow (envPanel,  { { &attack, &attackL }, { &decay, &decayL },
                           { &sustain, &sustainL }, { &release, &releaseL } });

    slot  .setBounds (kLeftX, kSelectY, kActionsW, size::controlH);
    legato.setBounds (kLeftX + kActionsW + space::s4, kSelectY, 140, size::controlH);

    grid.setBounds (kLeftX, kGridY, StepGridView::width,
                    StepGridView::heightFor (juce::jmax (1, ui.length)));

    /* "The Hint bar pinned to the bottom edge" -- flush, not inside the
     * window padding, with its rule along the top. */
    hint.setBounds (0, getHeight() - HintBar::height, getWidth(), HintBar::height);
}
