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
 * Ring 240 + space-6 + plot 104 comes to 368, which is also two 172px panels
 * with space-6 between them, so the two columns end level. That is the only
 * reason the plot is 104 and not a rounder number -- it was 92 until the
 * envelope gained a millisecond axis, which needed its own strip and was not
 * going to take it from the curve.
 */
constexpr int kPad        = space::s8;
constexpr int kRing       = 240;
constexpr int kPlotH      = 104;
constexpr int kActionsW   = 96;
constexpr int kTopBlockH  = kRing + space::s6 + kPlotH;          /* 368 */
constexpr int kPanelH     = (kTopBlockH - space::s6) / 2;        /* 172 */

constexpr int kLeftX      = kPad;
constexpr int kBlockX     = kPad + kRing + space::s8;            /* 304 */
constexpr int kBlockW     = StepGridView::width - kRing - space::s8;  /* 488 */
constexpr int kPanelW     = kBlockW - kActionsW - space::s4;     /* 376 */
constexpr int kActionsX   = kBlockX + kPanelW + space::s4;

constexpr int kSelectY    = kPad + kTopBlockH + space::s6;       /* 424 */

/*
 * The pattern plot spans the grid exactly -- same x, same width -- because it
 * is the same 128 steps drawn as time instead of as pads, and anything else
 * would read as a coincidence. Its height is the envelope plot's, so the
 * window has ONE plot height rather than two nearly-equal ones; that is also
 * Its HEIGHT used to be the envelope plot's, so the window had one plot
 * height rather than two nearly-equal ones. They differ now because one of
 * them has a millisecond axis and the other does not -- a reason, rather than
 * the drift that rule existed to prevent. Handing the pattern plot 12px it
 * has nothing to draw in would also have pushed a 128-step window past the
 * 1000px the size test allows.
 */
constexpr int kPatternH   = 92;
constexpr int kPatternY   = kSelectY + size::controlH + space::s2;   /* 460 */
constexpr int kGridY      = kPatternY + kPatternH + space::s4;   /* 568 */
}

/* ====================================================== envelope curve == */

TranceGateEditor::EnvelopeCurve::EnvelopeCurve (TranceGateProcessor& p) : proc (p) {}

void TranceGateEditor::EnvelopeCurve::refresh (double stepMs)
{
    /* Everything that changes the shape, and nothing that does not: the
     * playhead moves 30 times a second and must not cause a re-render. */
    const juce::String now = proc.engineGet ("attack") + "/" + proc.engineGet ("decay")
                           + "/" + proc.engineGet ("sustain") + "/" + proc.engineGet ("release")
                           + "/" + proc.engineGet ("hold") + "/" + juce::String (stepMs, 2)
                           + "/" + proc.engineGet ("slot")
                           /* The mode changes what the three stage numbers
                            * MEAN without changing one of them, so without it
                            * in the stamp the curve would keep the old
                            * shape. */
                           + "/" + proc.engineGet ("time_mode");
    if (now == stamp) return;
    stamp = now;
    shape = TgEnvelopeShape::render (proc, stepMs);
    builtFor = 0;
    repaint();
}

void TranceGateEditor::EnvelopeCurve::rebuild (int cols)
{
    builtFor = cols;
    plot::decimate (shape.env.data(), (int) shape.env.size(), cols, lo, hi);
    curve.build (lo, hi);

    ghost = {};
    if (! shape.envDialled.empty())
    {
        plot::decimate (shape.envDialled.data(), (int) shape.envDialled.size(), cols, lo, hi);
        ghost.build (lo, hi);
    }
}

void TranceGateEditor::EnvelopeCurve::paint (juce::Graphics& g)
{
    /* The caption is the two durations the picture is about: what the axis
     * covers, and what one step of it lasts. `hint` style, because it is a
     * note about the drawing rather than a value. */
    juce::String caption;
    if (shape.spanMs > 0.0)
        caption = "ENVELOPE " + juce::String (juce::roundToInt (shape.spanMs)) + " MS"
                + "   STEP " + juce::String (shape.msStep, 1) + " MS";

    const auto area = plot::well (g, getLocalBounds().toFloat(), caption);

    if (shape.env.empty())
    {
        plot::empty (g, getLocalBounds());
        return;
    }

    const int cols = juce::jmax (2, (int) area.getWidth());
    if (cols != builtFor) rebuild (cols);

    /* Two strips along the bottom -- the millisecond axis under the phase
     * letters -- so neither ever sits on top of the curve. Everything else is
     * drawn into what is left. */
    const float labelH = (float) font::hint().getHeight() + 2.0f;
    const auto  box    = area.withTrimmedBottom (labelH + (float) plot::axisHeight);

    /* Where the gate really closes: a rail-coloured hairline, since it marks
     * a position rather than a value. */
    if (shape.holdFrac < 1.0)
        plot::rule (g, box, shape.gateFrac(), colour::line200, stroke::hair);

    /*
     * THE GHOST FIRST, UNDERNEATH: the envelope as DIALLED, ignoring the gate.
     *
     * Only present when the gate closes before the decay has finished, which
     * is the case where the solid curve alone would be a puzzle -- you set a
     * 442 ms decay and the plot shows a spike. The dim line is the decay you
     * asked for; the solid one is what the gate leaves of it.
     */
    if (! ghost.empty())
        ghost.drawOutline (g, box, 0.0f, colour::inkDim, stroke::hair);

    curve.draw (g, box, 0.0f);

    /*
     * THE PHASE POINTS.
     *
     * Levels are SAMPLED FROM THE RENDERED CURVE rather than computed from
     * the millisecond values, so a dot cannot drift off the line it is
     * marking even if this file's idea of the envelope and the engine's ever
     * part company. ink, not phosphor: a phosphor dot on a phosphor curve is
     * invisible, and these are landmarks rather than something lit.
     */
    const double aEnd = shape.attackMs;
    const double dEnd = shape.attackMs + shape.decayMs;
    const double gate = shape.gateMs;
    const double rEnd = shape.gateMs + shape.releaseMs;

    auto dotAt = [&] (double ms)
    {
        const double f = juce::jlimit (0.0, 1.0, ms / shape.spanMs);
        const float  x = box.getX() + box.getWidth() * (float) f;
        const float  y = box.getBottom() - box.getHeight() * shape.levelAt (f);
        g.setColour (colour::ink);
        g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
    };

    /* A stage the gate never reached did not happen, so it gets no marker --
     * and env_enter walks zero-length stages without emitting a sample, so a
     * zero-length one has no point to mark either. */
    dotAt (0.0);
    if (aEnd > 0.0 && aEnd <= gate)             dotAt (aEnd);
    if (shape.decayMs > 0.0 && dEnd <= gate)    dotAt (dEnd);
    dotAt (gate);
    if (shape.releaseMs > 0.0 && rEnd <= shape.spanMs) dotAt (rEnd);

    /*
     * A / D / S / R under the segment each names, dropped when its segment is
     * narrower than the letter. At 227px a fast attack is a couple of pixels
     * wide and four letters would simply overprint each other.
     */
    const auto fh = font::hint();
    const std::pair<const char*, std::pair<double, double>> segs[] = {
        { "A", { 0.0,  aEnd } }, { "D", { aEnd, dEnd } },
        { "S", { dEnd, gate } }, { "R", { gate, rEnd } },
    };
    for (const auto& seg : segs)
    {
        const double from = juce::jlimit (0.0, shape.spanMs, seg.second.first);
        const double to   = juce::jlimit (0.0, shape.spanMs, seg.second.second);
        if (to <= from) continue;

        const float x0 = box.getX() + box.getWidth() * (float) (from / shape.spanMs);
        const float x1 = box.getX() + box.getWidth() * (float) (to   / shape.spanMs);
        const float w  = trackedWidth (seg.first, fh, font::trackHint);
        if (x1 - x0 < w + 4.0f) continue;

        drawTracked (g, seg.first, fh, colour::inkMuted,
                     { (x0 + x1) * 0.5f - w * 0.5f,
                       area.getBottom() - (float) plot::axisHeight - 1.0f },
                     font::trackHint);
    }

    /*
     * THE STEP EDGE, drawn last so it is not buried, and thicker than the
     * gate because it is the hard boundary: past here the engine has moved on
     * to the next step whatever the envelope was doing.
     *
     * Amber when the release cannot finish inside the step. The system
     * reserves amber for "armed / about to clip / not right", and a release
     * with nowhere to go is exactly that -- and it is the one amber mark in
     * the window, which is the limit the system sets.
     */
    plot::rule (g, box, shape.stepFrac(),
                shape.truncated ? colour::amber : colour::line200, stroke::rail);

    /* The axis last, in the strip below the letters. The step edge is handed
     * over as the landmark that must keep its exact value -- it is the number
     * that says whether a release fits. */
    plot::axis (g, area, shape.spanMs, shape.msStep);
}

/* ======================================================= pattern curve == */

TranceGateEditor::PatternCurve::PatternCurve (TranceGateProcessor& p) : proc (p) {}

void TranceGateEditor::PatternCurve::refresh (const juce::String& uiRaw, double stepMs)
{
    /*
     * THE STAMP IS THE READOUT WITH THE VOLATILE FIELDS TAKEN OUT.
     *
     * ui is steps:ties:length:phase:ms_step:advancing:cursor:depths. Fields 3,
     * 5 and 6 change constantly while the transport runs, and re-rendering a
     * 32k-sample pattern at 30 Hz to draw the same curve would be absurd, so
     * the pattern's shape is fields 0, 1, 2 and 7 only.
     *
     * ms_step goes in as a number rather than the rate label because a TEMPO
     * change also changes the picture, and is quantised to 0.1 ms so a host
     * automating tempo does not re-render every frame. Amount is deliberately
     * absent: it is applied in the paint transform.
     */
    const auto parts = juce::StringArray::fromTokens (uiRaw, ":", "");
    juce::String now;
    for (int i : { 0, 1, 2, 7 })
        now += (i < parts.size() ? parts[i] : juce::String()) + ":";

    /* THE SLOT IS IN THE STAMP EXPLICITLY, not left to the fields above.
     * They cover it only by accident -- two slots holding the same pattern
     * and differing in their sound would render once and stay wrong. */
    now += proc.engineGet ("slot") + "|"
         + juce::String (stepMs, 1) + "|" + proc.engineGet ("legato")
         + "|" + proc.engineGet ("attack")  + "|" + proc.engineGet ("decay")
         + "|" + proc.engineGet ("sustain") + "|" + proc.engineGet ("release")
         + "|" + proc.engineGet ("hold");

    if (now == stamp) return;
    stamp = now;
    shape = TgPatternShape::render (proc, stepMs, juce::jmax (2, getWidth() - 2 * plot::inset - 1));
    builtFor = 0;
    repaint();
}

void TranceGateEditor::PatternCurve::setPlayhead (double stepPhase, bool isMoving)
{
    if (juce::approximatelyEqual (stepPhase, phase) && isMoving == moving) return;
    phase  = stepPhase;
    moving = isMoving;
    repaint();
}

void TranceGateEditor::PatternCurve::setAmount (float a)
{
    if (juce::approximatelyEqual (a, amount)) return;
    amount = a;
    repaint();          /* the floor is a transform -- no rebuild, no render */
}

void TranceGateEditor::PatternCurve::rebuild (int cols)
{
    builtFor = cols;
    plot::decimate (shape.gain.data(), (int) shape.gain.size(), cols, lo, hi);
    curve.build (lo, hi);
}

void TranceGateEditor::PatternCurve::paint (juce::Graphics& g)
{
    juce::String caption;
    if (shape.length > 0)
        caption = "PATTERN " + juce::String (shape.length) + " STEPS"
                + "   x " + juce::String (shape.msStep, 1) + " MS";

    const auto area = plot::well (g, getLocalBounds().toFloat(), caption);

    if (shape.gain.empty() || shape.length <= 0)
    {
        plot::empty (g, getLocalBounds());
        return;
    }

    const int cols = juce::jmax (2, (int) area.getWidth());
    if (cols != builtFor) rebuild (cols);

    /*
     * BARS AND BEATS, NOT 128 RULES.
     *
     * At 128 steps a rule per step is one every 5.8 px and the plot turns
     * into a comb. Every 16th in rail and every 4th in hairline is a rule
     * every 23 px at that length, and it is the same reading StepGridView
     * offers by bordering every fourth step. Below 8 steps there is room for
     * all of them.
     */
    const int n = shape.length;
    for (int i = 1; i < n; ++i)
    {
        const bool bar  = (i % 16) == 0;
        const bool beat = (i % 4) == 0;
        if (! bar && ! beat && n >= 8) continue;

        plot::rule (g, area, (double) i / (double) n,
                    bar ? colour::line200 : colour::line100, stroke::hair);
    }

    /*
     * THE AMOUNT FLOOR. Amount is a dry/wet, so at 40% a shut gate still
     * passes 60% of the signal -- the gate never closes below this line. Drawn
     * as the rule it is rather than with a number, because the Amount knob
     * already prints one.
     */
    const float floorLevel = juce::jlimit (0.0f, 1.0f, 1.0f - amount);

    curve.draw (g, area, floorLevel);

    if (floorLevel > 0.001f)
    {
        const float y = area.getBottom() - area.getHeight() * floorLevel;
        g.setColour (colour::line200);
        g.drawLine (area.getX(), y, area.getRight(), y, stroke::hair);
    }

    /* The playhead is "here", not "on" -- ink, like the ring's, and never
     * phosphor, which the system spends only on what is lit. */
    if (moving)
        plot::rule (g, area, phase / (double) n, colour::ink, stroke::hair);
}

/* ================================================================ editor == */

int TranceGateEditor::heightFor (int length)
{
    return kGridY + StepGridView::heightFor (length) + space::s6 + HintBar::height;
}

int TranceGateEditor::plotStripTop()    { return kPatternY - space::s2; }
int TranceGateEditor::plotStripHeight() { return kPatternH + 2 * space::s2; }

TranceGateEditor::TranceGateEditor (TranceGateProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&phosphorLook);

    addAndMakeVisible (gatePanel);
    addAndMakeVisible (envPanel);

    wireKnob (gatePanel, rate,    rateL,    "rate");
    wireKnob (gatePanel, length,  lengthL,  "length");
    wireKnob (gatePanel, amount,  amountL,  "amount");
    wireKnob (gatePanel, width,   widthL,   "hold");
    wireKnob (envPanel,  attack,  attackL,  "attack");
    wireKnob (envPanel,  decay,   decayL,   "decay");
    wireKnob (envPanel,  sustain, sustainL, "sustain");
    wireKnob (envPanel,  release, releaseL, "release");
    addAndMakeVisible (ring);
    addAndMakeVisible (envelope);
    addAndMakeVisible (pattern);
    addAndMakeVisible (grid);
    addAndMakeVisible (hint);

    /* The pattern Select, which the StepGrid card puts above-left of the
     * grid. On Move this is the slot; here it is the same thing named the way
     * the system names it. */
    slot.addItemList ({ "1","2","3","4","5","6","7","8" }, 1);
    addAndMakeVisible (slot);
    slotAtt = std::make_unique<ComboAtt> (proc.state(), "slot", slot);

    /*
     * ms OR % OF THE STEP, beside the envelope it governs.
     *
     * A ComboBox and not a toggle: the two readings are named things, and a
     * switch labelled "%" leaves you guessing what the other position means.
     */
    timeMode.addItemList ({ "ms", "% Step" }, 1);
    addAndMakeVisible (timeMode);
    addAndMakeVisible (timeModeL);
    timeModeAtt = std::make_unique<ComboAtt> (proc.state(), "time_mode", timeMode);
    timeMode.onChange = [this] { refreshStageText(); };

    /* The envelope's SHAPE, beside its unit -- both say how the envelope is
     * measured or drawn rather than what its values are, and the envelope
     * panel's four knobs leave no room for either. */
    curve.addItemList ({ "Linear", "Exponential", "S-Curve" }, 1);
    addAndMakeVisible (curve);
    addAndMakeVisible (curveL);
    curveAtt = std::make_unique<ComboAtt> (proc.state(), "curve", curve);
    /* A shape change redraws both plots: the envelope's path is different and
     * the pattern's gain follows it. Neither value moved, so nothing else
     * would have told them. */
    curve.onChange = [this] { refreshStageText(); };

    legato.setButtonText ("Join Neighbors");
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

        /*
         * ACTIVATING A DEAD PAD GIVES IT ITS FULL AMOUNT.
         *
         * The amount is independent of the on/off mask, so a pad dragged down
         * to 20% once came back at 20% every time it was switched on again --
         * which reads as the click having half-worked. Only on OFF -> ON:
         * On<->Tie changes what a live step does rather than activating a
         * dead one, and switching off must not discard an amount that was set
         * on purpose.
         */
        if (! on) proc.engineSet ("step_amount", "1.000");
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

    /*
     * 270 DEGREES, WHICH IS THE SYSTEM'S SWEEP AND NOT JUCE'S.
     *
     * JUCE's default is 1.2pi -> 2.8pi, which is 288, and nothing here ever
     * said otherwise -- so every knob drew a rail 18 degrees too long with
     * its gap 9 degrees off on each side. The Knob card's own SVG,
     *
     *     <path class="rail" d="M9.86 38.14 A20.0 20.0 0 1 1 38.14 38.14"/>
     *
     * starts at (9.86, 38.14), which about a centre of (24, 24) is 225
     * degrees, and sweeps 270 to 495. drawRotarySlider already draws the
     * angles it is handed, so this is the only place that has to know them.
     */
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                           juce::MathConstants<float>::pi * 2.75f, true);

    /* "Never draw a knob without its readout: the arc shows position, the
     * readout shows the number." The box is a PhosphorReadout, supplied by
     * the LookAndFeel, so it prints the unit in ink-muted.
     *
     * The width passed here is the card's MINIMUM, not the column's:
     * getSliderLayout widens the box to the slider it is given, so a number
     * clips only when the column is narrower than a readout is allowed to
     * be. It used to be a bare 74 -- the column width from resized() copied
     * out by hand, two numbers that had to agree with nothing to notice when
     * they stopped. */
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, size::readout, size::controlH);

    /* The attachment sets the range, the value, the double-click default and
     * the two-way link. Setting any of those by hand here would be a second
     * opinion about the parameter -- and would be overwritten on the next
     * line regardless. */
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
    const double ph   = livePhase();
    const int    head = (int) std::floor (ph);

    /* Here rather than in timerCallback because the two grid-edit callbacks
     * also come through pushModels: a toggled pad redraws the curve in the
     * same pass instead of a frame later. refresh() is a string compare
     * unless something that shapes the pattern actually moved. */
    pattern.refresh (lastRaw, ui.msStep);
    pattern.setPlayhead (ph, ui.moving);
    /* An atomic read, not engineGet: engineGet takes the engine lock and
     * allocates TG_STATE_MAX bytes, which is not what a 30 Hz poll should
     * cost for one float. */
    pattern.setAmount (proc.state().getRawParameterValue ("amount")->load());

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

/*
 * A mode change alters what every stage's number MEANS without altering the
 * number, so no parameter value changes and nothing tells the sliders their
 * text is stale. Slider::updateText re-asks the parameter, which is exactly
 * what is wanted and is why the formatting lives there.
 */
void TranceGateEditor::refreshStageText()
{
    attack.updateText();
    decay.updateText();
    release.updateText();
    /* The envelope's SHAPE changes too: in % the stages are a share of the
     * step, so the same numbers draw a different curve. */
    envelope.refresh (ui.msStep);
    repaint();
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
                           { &amount, &amountL }, { &width, &widthL } });
    placeRow (envPanel,  { { &attack, &attackL }, { &decay, &decayL },
                           { &sustain, &sustainL }, { &release, &releaseL } });

    slot  .setBounds (kLeftX, kSelectY, kActionsW, size::controlH);
    legato.setBounds (kLeftX + kActionsW + space::s4, kSelectY, 172, size::controlH);
    /* The envelope's unit, in the row's spare width on the right -- it
     * belongs with the envelope, and the envelope panel has no room. */
    /* CURVE in the row's free middle, between the Legato toggle and the Env
     * Time pair on the right. */
    curveL  .setBounds (kLeftX + kActionsW + space::s4 + 172 + space::s6,
                        kSelectY, 60, size::controlH);
    curve   .setBounds (kLeftX + kActionsW + space::s4 + 172 + space::s6 + 60 + space::s2,
                        kSelectY, 124, size::controlH);
    timeModeL.setBounds (kActionsX - 108 - space::s2, kSelectY, 100, size::controlH);
    timeMode .setBounds (kActionsX, kSelectY, kActionsW, size::controlH);

    pattern.setBounds (kLeftX, kPatternY, StepGridView::width, kPatternH);

    grid.setBounds (kLeftX, kGridY, StepGridView::width,
                    StepGridView::heightFor (juce::jmax (1, ui.length)));

    /* "The Hint bar pinned to the bottom edge" -- flush, not inside the
     * window padding, with its rule along the top. */
    hint.setBounds (0, getHeight() - HintBar::height, getWidth(), HintBar::height);
}
