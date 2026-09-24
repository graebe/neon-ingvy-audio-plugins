#include "UvComponents.h"

using namespace uv;

/* =============================================================== panel == */

PanelBox::PanelBox (juce::String t) : name (std::move (t)) {}

juce::Rectangle<int> PanelBox::contentArea() const
{
    return getLocalBounds().reduced (space::s4).withTrimmedTop (titleBlock);
}

void PanelBox::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colour::bg100);
    g.fillRect (r);
    g.setColour (colour::line100);
    g.drawRect (r, stroke::hair);

    const auto f = font::title();
    drawTracked (g, name.toUpperCase(), f, colour::inkMuted,
                 { (float) space::s4, (float) space::s4 + f.getHeight() },
                 font::trackTitle);
}

/* ================================================================ ring == */

void RingDisplay::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat();
    const float d  = juce::jmin (box.getWidth(), box.getHeight());
    const auto c   = box.getCentre();

    /*
     * ANNULAR SECTORS, NOT STROKED ARCS.
     *
     * The outer edge is where it always was -- 114 of 240, the stroked band's
     * outer limit -- and the inner edge has come in a long way, so a segment
     * is a wedge you can read at a glance rather than a tick. Radii are
     * fractions of 240 so the 120px variant is the same drawing at half size.
     *
     * The decorative hairline that used to sit at 99 is gone: the sectors now
     * cover it at low step counts and would have shown THROUGH the band at
     * high ones, and the unlit sectors already mark the ring's full extent,
     * which is all it was there for.
     */
    const float rOuter = d * (114.0f / 240.0f);
    const int   n      = juce::jmax (1, model.length);

    /*
     * STROKE FALLS AWAY AS THE COUNT RISES.
     *
     * The card gives stroke-width 12 at 24 segments. At 128 the slot is 2.8
     * degrees, and twelve pixels of stroke on a 108px radius is wider than
     * the slot -- the segments merge into a solid disc and the pattern stops
     * being readable, which is the one thing the ring is for.
     *
     * This is not the scaling Principle 4 forbids. That rule is about
     * CONTROLS having one size; the ring is a display whose segment count is
     * the pattern's, not a size anyone chose.
     */
    /*
     * THE BAND NARROWS AS THE COUNT RISES, FROM THE INSIDE.
     *
     * A fat wedge is the point at 16 steps. At 128 the slot is 2.8 degrees
     * and the gap between wedges is about a pixel, so a 34px-deep band would
     * read as one solid annulus and the pattern -- the only thing the ring is
     * for -- would be gone. Pulling the INNER edge back out keeps the outer
     * circle fixed, so the ring does not appear to change size with Length.
     */
    const float depth = juce::jlimit (8.0f, 34.0f, 900.0f / (float) n) * (d / 240.0f);
    const float inner = juce::jmax (0.0f, (rOuter - depth) / rOuter);
    const float slot = juce::MathConstants<float>::twoPi / (float) n;
    const float fill = slot * 0.8f;             /* 12 degrees of 15 in the preview */

    for (int i = 0; i < n; ++i)
    {
        /* The wedge says on or off only -- the playhead is its own mark,
         * drawn after this loop. */
        const bool on = model.stepOn && model.stepOn (i);

        /*
         * NO -halfPi HERE. addCentredArc's fromRadians is documented as
         * "the angle (clockwise) ... where 0 is the top-centre", so the
         * convention is ALREADY a step sequencer's reading order. Subtracting
         * a quarter turn on top of it -- which is what you need when you are
         * converting raw trigonometry, where 0 is three o'clock -- turned the
         * whole ring counter-clockwise by 90 degrees: step 0 sat at nine
         * o'clock and the step drawn at the top was n/4. The pattern was
         * right, the playhead was right, and both were in the wrong place.
         *
         * ui_chain.js:523 is the same formula on the same convention and has
         * never had the offset.
         */
        const float a0 = (float) i * slot + (slot - fill) * 0.5f;

        /* addPieSegment takes the same top-centre clockwise angles
         * addCentredArc did, so the reading order above still holds. */
        juce::Path p;
        p.addPieSegment (juce::Rectangle<float> (rOuter * 2.0f, rOuter * 2.0f).withCentre (c),
                         a0, a0 + fill, inner);
        /*
         * THE WEDGE SAYS ON OR OFF, AND NOTHING ELSE.
         *
         * It used to carry the playhead too, by shading: ink over a lit step,
         * a glow wash over a gap. That worked only while the signal was dim
         * enough for ink to be visibly brighter. It is not any more -- uv and
         * ink are 1.34:1 -- so the playhead is a separate mark below, which
         * is how ui_chain.js has always drawn it on the Move and is sturdier
         * than any hue difference.
         *
         * AN UNLIT SECTOR IS THE RAIL, NOT DIM TEXT. It was inkDim, which the
         * system reserves for disabled text and which sat close enough to the
         * signal that on and off read as two brightnesses of one thing.
         * line-200 is the token for "the unlit part of an arc" -- exactly what
         * this is.
         */
        /*
         * NO RIM. This carried a uvDeep stroke as the "fill drop-shadow" the
         * system asks for, but a constant-alpha 5px stroke has a hard edge
         * and reads as a BORDER around every lit wedge rather than as light
         * coming off one.
         *
         * The wedges therefore carry no violet of their own, the cast living
         * entirely in the halo elsewhere. If it is wanted back here, the
         * answer is a falloff -- halo() in Uv.cpp fades with distance -- and
         * not a stroke at one alpha.
         */
        g.setColour (on ? colour::uv : colour::line200);
        g.fillPath (p);
    }

    /*
     * THE PLAYHEAD, AS ITS OWN MARK. A dot on the ring's inner edge at the
     * step being played -- ink, because it is "here" rather than "on", and
     * the one place the ring spends the brightest token. It reads against a
     * lit wedge and an unlit one alike, which is the whole point of taking it
     * out of the wedge's colour.
     */
    if (model.moving && model.playhead >= 0)
    {
        const float a = ((float) model.playhead + 0.5f) * slot
                      - juce::MathConstants<float>::halfPi;   /* raw trig: 0 is 3 o'clock */
        const float rDot = rOuter * inner - d * (7.0f / 240.0f);
        const auto  dot  = juce::Rectangle<float> (d * (7.0f / 240.0f), d * (7.0f / 240.0f))
                             .withCentre ({ c.x + std::cos (a) * rDot,
                                            c.y + std::sin (a) * rDot });
        glowLed (g, dot, dot.getWidth() * 0.5f);
        g.setColour (colour::ink);
        g.fillEllipse (dot);
    }

    /* The window's one readout-size number, with a label under it. */
    const auto fr = font::readout();
    g.setFont (fr);
    g.setColour (colour::ink);
    g.drawText (model.centre, getLocalBounds().withTrimmedBottom (20),
                juce::Justification::centred);

    const auto fl = font::label();
    const float w = trackedWidth (model.label, fl, font::trackLabel);
    drawTracked (g, model.label, fl, colour::inkMuted,
                 { c.x - w * 0.5f, c.y + 22.0f }, font::trackLabel);
}

/* ============================================================ stepgrid == */

int StepGridView::heightFor (int length)
{
    const int rows = rowsFor (length);
    return rows * size::step + (rows - 1) * space::s2;
}

juce::Rectangle<int> StepGridView::stepBounds (int i) const
{
    const int row = i / cols, col = i % cols;
    return { col * (size::step + space::s2), row * (size::step + space::s2),
             size::step, size::step };
}

int StepGridView::stepAt (juce::Point<int> p) const
{
    for (int i = 0; i < model.length; ++i)
        if (stepBounds (i).contains (p)) return i;
    return -1;
}

void StepGridView::paint (juce::Graphics& g)
{
    for (int i = 0; i < model.length; ++i)
    {
        const auto r = stepBounds (i).toFloat().reduced (0.5f);
        const bool on  = model.stepOn   && model.stepOn (i);
        const bool tie = model.stepTied && model.stepTied (i);
        const bool at  = model.moving && i == model.playhead;
        const float amt = model.stepAmount ? model.stepAmount (i) : 1.0f;

        /* `.ph-step.on` and `.ph-step.play` both carry glow-led in the
         * system's stylesheet: the halo is how a near-white fill keeps its
         * violet, so a lit step needs it as much as the playhead does. */
        if (on || at) glowLed (g, r);

        if (tie)
        {
            /* A hollow uv outline with a bar across it -- the tie says
             * "this step holds through", so it is not a fill. */
            g.setColour (colour::bg200);
            g.fillRect (r);
            g.setColour (colour::uv);
            g.drawRect (r, stroke::hair);
            g.drawRect (r.reduced (1.0f), stroke::hair);
            g.fillRect (r.getX(), r.getCentreY() - 1.0f, r.getWidth(), 2.0f);
        }
        else if (on)
        {
            /*
             * THE AMOUNT IS THE LIT HEIGHT, FROM THE BOTTOM.
             *
             * The system disagrees with itself here: Step/README.md says the
             * fill grows from the bottom, bundle.css puts the dark block at
             * bottom:0 so it grows down from the top. The README wins, both
             * because a lit height reading as a level is how every step
             * sequencer works and because it is what the hardware does. The
             * system's CSS is the side worth correcting.
             */
            g.setColour (colour::bg200);
            g.fillRect (r);
            const float h = juce::jmax (2.0f, r.getHeight() * juce::jlimit (0.0f, 1.0f, amt));
            const auto lit = r.withTop (r.getBottom() - h);
            g.setColour (colour::uv);
            g.fillRect (lit);

            /*
             * THE PLAYHEAD, ON A PAD THAT IS ALREADY LIT.
             *
             * Nothing here used to read `at`. The only thing separating a
             * playing lit pad from any other was the halo -- and once every
             * lit pad gained one (the system gives `.ph-step.on` glow-led)
             * that distinction went with it, so the playhead became invisible
             * on exactly the pads it most needs to be seen on.
             *
             * Darkening the LIT PART is the cheapest honest answer: the pad
             * is still on, still the same height, and the one that is
             * sounding is the one that dips. onUv is the token for "what goes
             * on top of a uv fill", and at a quarter alpha it reads as a
             * shadow crossing the row rather than as a different state.
             *
             * NOT uvDeep: the system says it is never a fill, and spending it
             * here would collapse the two-tone it exists to protect.
             */
            if (at)
            {
                g.setColour (colour::onUv.withAlpha (0.25f));
                g.fillRect (lit);
            }

            g.setColour (colour::uv);
            g.drawRect (r, stroke::hair);
        }
        else
        {
            g.setColour (at ? colour::bg300 : colour::bg200);
            g.fillRect (r);
            /* The playhead on an OFF step is a uv-glow wash: the step
             * is not on, so it must not be uv. */
            if (at)
            {
                g.setColour (colour::uvGlow);
                g.fillRect (r);
            }
            /* Every fourth step carries a rail-coloured border, so bars read
             * without numbers. */
            g.setColour (at ? colour::uv : (i % 4 == 0 ? colour::line200 : colour::line100));
            g.drawRect (r, stroke::hair);
        }

        /* The cursor -- the step the knobs edit. Not a system state (its five
         * are off/on/hover/active/disabled), so it is drawn as the system
         * draws selection everywhere else: in uv, here as a mark
         * outside the well so it cannot be confused with a lit step. */
        if (i == model.cursor)
        {
            g.setColour (colour::ink);
            g.drawRect (r.expanded (2.0f), stroke::hair);
        }
    }
}

void StepGridView::mouseDown (const juce::MouseEvent& e)
{
    const int i = stepAt (e.getPosition());
    if (i >= 0 && onToggle) onToggle (i, e.mods.isShiftDown());
}

void StepGridView::mouseDrag (const juce::MouseEvent& e)
{
    /*
     * A CLICK IS NOT A DRAG, however much the hand shakes.
     *
     * JUCE sends mouseDrag after a click with a pixel of jitter, and this
     * sets the step's amount straight from the pointer's y -- so clicking
     * near the bottom of a cell brought the step on at 10% and looked like
     * the pad had half-failed to light. mouseWasDraggedSinceMouseDown is
     * JUCE's own threshold for exactly this question, which is better than a
     * number invented here.
     */
    if (! e.mouseWasDraggedSinceMouseDown()) return;

    const int i = stepAt (e.getMouseDownPosition());
    if (i < 0 || ! onAmount) return;
    const auto b = stepBounds (i);
    onAmount (i, juce::jlimit (0.0f, 1.0f,
                               1.0f - (e.position.y - (float) b.getY()) / (float) b.getHeight()));
}

/* ================================================================ hint == */

void HintBar::setClauses (std::vector<std::pair<juce::String, juce::String>> c)
{
    /* Three at most. Truncating rather than shrinking the text is deliberate:
     * a fourth clause is a sign the window needs simplifying, not a smaller
     * font. */
    while (c.size() > 3) c.pop_back();
    clauses = std::move (c);
    repaint();
}

void HintBar::paint (juce::Graphics& g)
{
    g.setColour (colour::line100);
    g.fillRect (0, 0, getWidth(), 1);

    const auto f = font::hint();
    const juce::String sep (juce::CharPointer_UTF8 ("\xe2\x80\x93"));    /* en dash */
    const float gap = 12.0f;        /* .sep { margin: 0 12px } */

    /* Measure first: the bar is centred, and a two-pass draw is the only way
     * to centre a run of differently coloured pieces. */
    float total = 0.0f;
    for (int i = 0; i < (int) clauses.size(); ++i)
    {
        total += trackedWidth (clauses[(size_t) i].first + " " + clauses[(size_t) i].second, f, font::trackHint);
        if (i + 1 < (int) clauses.size())
            total += gap * 2.0f + juce::GlyphArrangement::getStringWidth (f, sep);
    }

    float x = (float) getWidth() * 0.5f - total * 0.5f;
    const float y = 1.0f + 8.0f + f.getHeight() * 0.8f;

    for (int i = 0; i < (int) clauses.size(); ++i)
    {
        x += drawTracked (g, clauses[(size_t) i].first + " ", f, colour::ink, { x, y }, font::trackHint)
           + f.getHeight() * font::trackHint;
        x += drawTracked (g, clauses[(size_t) i].second, f, colour::inkMuted, { x, y }, font::trackHint)
           + f.getHeight() * font::trackHint;

        if (i + 1 < (int) clauses.size())
        {
            x += gap;
            g.setColour (colour::inkDim);
            g.setFont (f);
            g.drawSingleLineText (sep, juce::roundToInt (x), juce::roundToInt (y));
            x += juce::GlyphArrangement::getStringWidth (f, sep) + gap;
        }
    }
}

/* =============================================================== scope == */

/*
 * THE GATE OVER THE AUDIO.
 *
 * The bands say what came out; this says what the gate was doing while it
 * did. Mirrored about the centre line, so it reads as the envelope the
 * waveform is sitting inside rather than as another signal -- and drawn last,
 * over everything, because it is the explanation and not the subject.
 *
 * The values are the pattern plot's, already rendered for this patch at this
 * width. Two renders of one thing could disagree; one cannot.
 */
static void drawGate (juce::Graphics& g, juce::Rectangle<float> area,
                      const std::vector<float>& gate)
{
    const int n = (int) gate.size();
    if (n < 2) return;

    const float mid = area.getCentreY();
    /* Short of the full half-height on purpose: a fully open gate drawn at
     * 1.0 lands exactly on the well's frame and reads as the border rather
     * than as a value. */
    const float half = area.getHeight() * 0.5f * 0.94f;

    juce::Path up, down;
    for (int i = 0; i < n; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) (n - 1);
        const float v = juce::jlimit (0.0f, 1.0f, gate[(size_t) i]);
        if (i == 0) { up.startNewSubPath (x, mid - half * v); down.startNewSubPath (x, mid + half * v); }
        else        { up.lineTo          (x, mid - half * v); down.lineTo          (x, mid + half * v); }
    }

    /*
     * A KEYLINE FIRST, THEN THE LINE.
     *
     * The dry band fills most of the well at close to full height, so there
     * is no brightness left to separate a gate at 1.0 from the audio it is
     * drawn over -- at hair weight it simply vanished into the band's top
     * edge. Cutting the well's own ground in behind the stroke gives the
     * gate its own edge against whatever it happens to cross, which is the
     * only thing that works when the background is a waveform.
     */
    g.setColour (colour::bg000);
    g.strokePath (up,   juce::PathStrokeType (stroke::rail + 2.0f));
    g.strokePath (down, juce::PathStrokeType (stroke::rail + 2.0f));

    g.setColour (colour::uv.withAlpha (0.85f));
    g.strokePath (up,   juce::PathStrokeType (stroke::rail));
    g.strokePath (down, juce::PathStrokeType (stroke::rail));
}

void ScopeView::paint (juce::Graphics& g)
{
    const auto area = plot::well (g, getLocalBounds().toFloat(), model.caption);

    if (model.columns <= 0 || model.dryLo == nullptr)
    {
        plot::empty (g, getLocalBounds());
        return;
    }

    const float mid = area.getCentreY();
    const float half = area.getHeight() * 0.5f;

    /* The step grid sits UNDER the audio, so a transient is never hidden by
     * a rule. Numbers and per-step rules thin out as the columns narrow. */
    plot::steps (g, area, model.steps, plot::Layer::rules);

    /* The zero line, so a silent stretch reads as silence rather than as a
     * gap in the drawing. */
    g.setColour (colour::line100);
    g.drawHorizontalLine ((int) mid, area.getX(), area.getRight());

    /*
     * A COLUMN OF THE CAPTURE IS NOT A COLUMN OF THE PLOT. The capture is a
     * fixed 512 and the plot is however wide the window made it, so each
     * pixel takes the union of the capture columns behind it -- the same
     * min/max union decimate does, and for the same reason: a peak that falls
     * between two pixels is still a peak that happened.
     */
    auto band = [&] (const std::atomic<float>* lo, const std::atomic<float>* hi,
                     juce::Colour c)
    {
        juce::Path p;
        const int w = juce::jmax (1, (int) area.getWidth());
        bool started = false;
        /* Out along the maxima... */
        for (int x = 0; x < w; ++x)
        {
            const int c0 = x * model.columns / w;
            const int c1 = juce::jmax (c0 + 1, (x + 1) * model.columns / w);
            if (c0 >= model.filled) break;
            float v = -2.0f;
            for (int i = c0; i < c1 && i < model.filled; ++i)
                v = juce::jmax (v, hi[i].load (std::memory_order_relaxed));
            const float px = area.getX() + (float) x;
            const float py = mid - half * juce::jlimit (-1.0f, 1.0f, v);
            if (! started) { p.startNewSubPath (px, py); started = true; }
            else           p.lineTo (px, py);
        }
        if (! started) return;
        /* ...and back along the minima, closing the band. */
        for (int x = juce::jmin (w, model.filled * w / model.columns) - 1; x >= 0; --x)
        {
            const int c0 = x * model.columns / w;
            const int c1 = juce::jmax (c0 + 1, (x + 1) * model.columns / w);
            float v = 2.0f;
            for (int i = c0; i < c1 && i < model.filled; ++i)
                v = juce::jmin (v, lo[i].load (std::memory_order_relaxed));
            p.lineTo (area.getX() + (float) x,
                      mid - half * juce::jlimit (-1.0f, 1.0f, v));
        }
        p.closeSubPath();
        g.setColour (c);
        g.fillPath (p);
    };

    /*
     * THE DRY IS CONTEXT, NOT THE SUBJECT, so it is drawn back at partial
     * alpha. A sustained input fills every column edge to edge -- min and max
     * ARE the full amplitude when a column spans a cycle -- so at full
     * strength it is a solid slab of ink-dim with the gated trace fighting to
     * be seen through it. The system's own rule applies: uv is the
     * signal, and everything else is quieter than it.
     */
    band (model.dryLo, model.dryHi, colour::inkDim.withAlpha (0.45f));
    band (model.wetLo, model.wetHi, colour::uv);

    if (model.gate != nullptr) drawGate (g, area, *model.gate);

    /* Last of all, so a number is never swallowed by the audio. */
    plot::steps (g, area, model.steps, plot::Layer::numbers);
}

/* =============================================================== grain == */

/*
 * THE GROUND: DOT PAPER UNDER NOISE, both from the system.
 *
 * "1px dots at a 12px (space-3) pitch, under a 5% uvDeep noise." The dots are
 * the structure and the noise is the texture; the noise is TINTED now rather
 * than the neutral grey it used to be, so the ground carries the same violet
 * cast as everything standing on it.
 *
 * The tile must be a whole number of pitches across or the pattern steps at
 * every tile seam -- 12 divides 120 and 132, not 128, so the caller's size is
 * rounded to the nearest multiple here rather than trusted.
 */
juce::Image makeGrain (int size, float maxAlpha, juce::Random& rng)
{
    const int pitch = space::s3;
    const int side  = juce::jmax (pitch, (size / pitch) * pitch);

    juce::Image img (juce::Image::ARGB, side, side, true);
    juce::Image::BitmapData px (img, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            px.setPixelColour (x, y, colour::uvDeep.withAlpha (rng.nextFloat() * maxAlpha));

    /* The dots sit on top of the noise, at full strength: they are structure,
     * not texture, and at 1px on a 12px pitch they are sparse enough to read
     * as paper rather than as a grid.
     *
     * OFFSET HALF A PITCH, as the system's own `background-position: 6px 6px`
     * has it. It matters at the window's edge: on the zero offset a dot lands
     * in the very corner and on the frame, which reads as a stray pixel
     * rather than as paper. */
    for (int y = pitch / 2; y < side; y += pitch)
        for (int x = pitch / 2; x < side; x += pitch)
            px.setPixelColour (x, y, colour::bgDot);

    return img;
}

/* ================================================================ knob == */

/* =============================================================== tabs == */

void UvTabs::setTabs (juce::StringArray labels)
{
    tabs = std::move (labels);
    current = juce::jlimit (0, juce::jmax (0, tabs.size() - 1), current);
    repaint();
}

void UvTabs::setActive (int index, juce::NotificationType note)
{
    const int i = juce::jlimit (0, juce::jmax (0, tabs.size() - 1), index);
    if (i == current) return;
    current = i;
    repaint();
    if (note != juce::dontSendNotification && onChange) onChange (current);
}

juce::Rectangle<int> UvTabs::tabBounds (int i) const
{
    const int n = juce::jmax (1, tabs.size());
    const int h = getHeight() / n;
    /* The last tab takes the remainder, so N tabs always fill the strip
     * exactly however the height divides. */
    return { 0, i * h, getWidth(), (i == n - 1) ? getHeight() - i * h : h };
}

int UvTabs::indexAt (juce::Point<int> p) const
{
    for (int i = 0; i < tabs.size(); ++i)
        if (tabBounds (i).contains (p)) return i;
    return -1;
}

void UvTabs::paint (juce::Graphics& g)
{
    const auto f = font::hint();

    for (int i = 0; i < tabs.size(); ++i)
    {
        const auto r  = tabBounds (i).toFloat().reduced (0.5f);
        const bool on = (i == current);

        if (on) glowLed (g, r);
        g.setColour (on ? colour::uv : (i == hovered ? colour::bg300 : colour::bg200));
        g.fillRect (r);
        g.setColour (on ? colour::uv : colour::line200);
        g.drawRect (r, stroke::hair);

        /* Rotated a quarter turn so it reads bottom-to-top, which is the way
         * a tab on a RIGHT edge is read. */
        const auto txt = tabs[i].toUpperCase();
        const float w  = trackedWidth (txt, f, font::trackHint);

        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi,
                                                         r.getCentreX(), r.getCentreY()));
        drawTracked (g, txt, f, on ? colour::onUv : colour::inkMuted,
                     { r.getCentreX() - w * 0.5f, r.getCentreY() + f.getHeight() * 0.35f },
                     font::trackHint);
    }
}

void UvTabs::mouseDown (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    if (i >= 0) setActive (i, juce::sendNotification);
}

void UvTabs::mouseMove (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    if (i != hovered) { hovered = i; repaint(); }
}

void UvTabs::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1) { hovered = -1; repaint(); }
}

/* =============================================================== knob == */

void UvKnob::setMagnets (double beatSteps, double barSteps)
{
    beatMag = juce::jmax (0.0, beatSteps);
    barMag  = juce::jmax (0.0, barSteps);
}

/*
 * A RUBBER BAND, NOT A DEAD ZONE -- and the difference is the whole feel.
 *
 * The obvious version is "within R of a magnet, return the magnet". It is
 * wrong twice: the values inside R stop being reachable at all, and leaving
 * the zone makes the number JUMP from the magnet to the far edge. A wall,
 * then a lurch.
 *
 * This warps instead:  d' = d * |d| / R.  It is monotonic (slope 2|d|/R) and
 * maps (-R, R) onto itself, so every value in the window is still reachable
 * -- but the slope goes to ZERO at the magnet, so the closer you get the more
 * travel it takes to move, and leaving is a smooth release. Because the
 * parameter is an integer, the practical effect is that a bar value occupies
 * several times the mouse travel its neighbours do: it sticks, and 15 is
 * still there if you want it. Odd lengths are a real trance-gate idiom, so
 * making them unreachable would have been a worse bug than the twitchiness.
 */
double UvKnob::magnetise (double attempted, double valuePerPixel) const
{
    const auto range = getRange();
    if (range.getLength() <= 0.0 || valuePerPixel <= 0.0) return attempted;

    /* Strong tier first: a bar boundary is usually also a beat boundary, and
     * where they coincide the firmer pull is the one that should be felt. */
    const double period[2] = { barMag,          beatMag };
    const double pullPx[2] = { barPullPx,       beatPullPx };

    for (int t = 0; t < 2; ++t)
    {
        if (period[t] <= 0.0) continue;

        const double m = std::round (attempted / period[t]) * period[t];
        /* A multiple outside the range is not a magnet -- without this, the
         * bar tier would invent one at the range's start, where the nearest
         * multiple happens to be zero. */
        if (m < range.getStart() || m > range.getEnd()) continue;

        const double R = pullPx[t] * valuePerPixel;
        const double d = attempted - m;
        if (R > 0.0 && std::abs (d) < R) return m + d * std::abs (d) / R;
    }
    return attempted;
}

double UvKnob::snapValue (double attempted, DragMode mode)
{
    /* A magnet is something you FEEL, so it exists only while a hand is on
     * the knob. Automation, a typed value and a preset load land exactly
     * where they were told. */
    if (mode == notDragging) return attempted;

    /* Shift already means `fine` here, which means "I want exactly this".
     * Read it LIVE rather than from mouseDown, so pressing or releasing it
     * part way through a drag takes effect at once -- which
     * setMouseDragSensitivity, set once on mouseDown, cannot do. */
    if (juce::ModifierKeys::getCurrentModifiers().isShiftDown()) return attempted;

    const int px = juce::jmax (1, getMouseDragSensitivity());
    return magnetise (attempted, getRange().getLength() / (double) px);
}

void UvKnob::mouseDown (const juce::MouseEvent& e)
{
    setMouseDragSensitivity (e.mods.isShiftDown() ? fine : coarse);
    juce::Slider::mouseDown (e);
}

/* =============================================================== label == */

TrackedLabel::TrackedLabel (juce::String t, bool asTitle)
    : text (std::move (t)), title (asTitle) {}

void TrackedLabel::paint (juce::Graphics& g)
{
    const auto f = title ? font::title() : font::label();
    const float tracking = title ? font::trackTitle : font::trackLabel;
    const auto up = text.toUpperCase();
    const float w = trackedWidth (up, f, tracking);
    drawTracked (g, up, f, colour::inkMuted,
                 { (float) getWidth() * 0.5f - w * 0.5f, f.getHeight() },
                 tracking);
}


/* ================================================================ plot == */

namespace uv::plot
{

juce::Rectangle<float> well (juce::Graphics& g, juce::Rectangle<float> bounds,
                             const juce::String& caption)
{
    const auto r = bounds.reduced (0.5f);
    g.setColour (colour::bg000);
    g.fillRect (r);
    g.setColour (colour::line100);
    g.drawRect (r, stroke::hair);

    if (caption.isNotEmpty())
        drawTracked (g, caption, font::hint(), colour::inkMuted,
                     { r.getX() + (float) inset, r.getY() + 12.0f }, font::trackHint);

    return r.reduced ((float) inset).withTrimmedTop ((float) captionH);
}

void empty (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour (colour::inkDim);
    g.setFont (font::hint());
    g.drawText ("...", bounds, juce::Justification::centred);
}

void rule (juce::Graphics& g, juce::Rectangle<float> plot, double xFrac,
           juce::Colour c, float thickness)
{
    const float x = plot.getX() + plot.getWidth() * (float) juce::jlimit (0.0, 1.0, xFrac);
    g.setColour (c);
    g.drawLine (x, plot.getY(), x, plot.getBottom(), thickness);
}

void axis (juce::Graphics& g, juce::Rectangle<float> plot, double spanMs,
           double markMs)
{
    if (spanMs <= 0.0 || plot.getWidth() < 8.0f) return;

    const auto f = font::hint();
    const float y = plot.getBottom() - (float) axisHeight;

    g.setColour (colour::line200);
    g.drawLine (plot.getX(), y, plot.getRight(), y, stroke::hair);

    /*
     * PRECISION PER LABEL, NOT PER VALUE.
     *
     * Deciding decimals from each number's own magnitude gave one axis
     * reading "0.0  5.0  10" -- three ticks of the same series printed two
     * ways. The ladder's ticks are round by construction so they take none;
     * the landmark is a measured duration and keeps its tenth, because
     * rounding 15.6 to "16" contradicts the caption three lines above it.
     */
    auto labelAt = [&] (double ms, bool withUnit, int decimals)
    {
        const auto txt = juce::String (ms, decimals) + (withUnit ? " ms" : "");
        const float x = plot.getX() + plot.getWidth() * (float) (ms / spanMs);
        const float w = trackedWidth (txt, f, font::trackHint);
        /* Nudged in at the ends so a label never hangs outside the well. */
        const float lx = juce::jlimit (plot.getX(), plot.getRight() - w, x - w * 0.5f);
        g.setColour (colour::line200);
        g.drawLine (x, y, x, y + 3.0f, stroke::hair);
        drawTracked (g, txt, f, colour::inkMuted,
                     { lx, plot.getBottom() - 1.0f }, font::trackHint);
        return juce::Range<float> (lx, lx + w);
    };

    /* The landmark first: it is exact, and everything else gives way to it. */
    juce::Range<float> taken;
    if (markMs > 0.0 && markMs <= spanMs)
    {
        const bool whole = std::abs (markMs - std::round (markMs)) < 0.05;
        taken = labelAt (markMs, true, whole ? 0 : 1);
    }

    static const double ladder[] = { 1, 2, 5, 10, 20, 25, 50, 100, 200, 250,
                                     500, 1000, 2000, 5000 };
    /* The smallest interval whose ticks stay ~44px apart -- about five across
     * a 228px plot, which is what the labels fit in. */
    double interval = ladder[std::size (ladder) - 1];
    for (double cand : ladder)
    {
        if (plot.getWidth() * (float) (cand / spanMs) >= 38.0f) { interval = cand; break; }
    }

    for (double ms = 0.0; ms <= spanMs + 1e-6; ms += interval)
    {
        /* The unit rides the landmark when there is one, so it appears once. */
        const bool withUnit = (taken.isEmpty() && ms + interval > spanMs);
        const auto txt = juce::String (ms, 0) + (withUnit ? " ms" : "");
        const float x = plot.getX() + plot.getWidth() * (float) (ms / spanMs);
        const float w = trackedWidth (txt, f, font::trackHint);
        const float lx = juce::jlimit (plot.getX(), plot.getRight() - w, x - w * 0.5f);
        /* A LADDER TICK GIVES WAY TO THE LANDMARK, and with room to spare:
         * at 4px of margin "0" and a step edge at 11% of the span rendered as
         * one run -- "0", a tick, then "62 ms" -- which reads as "0.062 ms"
         * and is worse than either label alone. */
        if (! taken.isEmpty()
            && juce::Range<float> (lx - 10.0f, lx + w + 10.0f).intersects (taken))
            continue;
        labelAt (ms, withUnit, 0);
    }
}

void decimate (const float* v, int n, int cols,
               std::vector<float>& lo, std::vector<float>& hi)
{
    lo.assign ((size_t) juce::jmax (0, cols), 0.0f);
    hi.assign ((size_t) juce::jmax (0, cols), 0.0f);
    if (v == nullptr || n <= 0 || cols <= 0) return;

    if (n < cols)
    {
        /* Interpolation, not decimation -- exact, because the envelope is
         * piecewise linear in time. */
        for (int c = 0; c < cols; ++c)
        {
            const double t = cols > 1 ? (double) c / (double) (cols - 1) : 0.0;
            const double x = t * (double) (n - 1);
            const int    i = juce::jlimit (0, n - 1, (int) x);
            const int    j = juce::jmin (n - 1, i + 1);
            const auto   f = (float) (x - (double) i);
            const float  y = v[i] + (v[j] - v[i]) * f;
            lo[(size_t) c] = hi[(size_t) c] = y;
        }
        return;
    }

    for (int c = 0; c < cols; ++c)
    {
        const int a = (int) ((double) c       * (double) n / (double) cols);
        const int b = (int) ((double) (c + 1) * (double) n / (double) cols);
        const int e = juce::jmax (a + 1, juce::jmin (n, b));

        float mn = v[a], mx = v[a];
        for (int i = a + 1; i < e; ++i)
        {
            mn = juce::jmin (mn, v[i]);
            mx = juce::jmax (mx, v[i]);
        }
        lo[(size_t) c] = mn;
        hi[(size_t) c] = mx;
    }
}

void steps (juce::Graphics& g, juce::Rectangle<float> plot, int count, Layer layer)
{
    if (count < 1 || plot.getWidth() <= 0.0f) return;

    constexpr float ruleMin   = 6.0f;    /* below this a rule per step is a comb */
    constexpr float numberMin = 14.0f;   /* below this the numbers touch         */

    const float w       = plot.getWidth() / (float) count;
    const bool  perStep = w >= ruleMin;
    const auto  f       = font::hint();

    if (layer == Layer::rules)
    for (int i = 1; i < count; ++i)
    {
        const bool bar  = (i % 16) == 0;
        const bool beat = (i % 4)  == 0;
        /* Bars and beats survive at any density -- they are what keeps a
         * 128-step picture readable once the per-step rules have gone. */
        if (! perStep && ! bar && ! beat) continue;

        const float x = plot.getX() + w * (float) i;
        g.setColour (bar ? colour::line200 : colour::line100);
        g.drawLine (x, plot.getY(), x, plot.getBottom(), stroke::hair);
    }

    if (layer == Layer::rules || w < numberMin) return;

    /*
     * Just right of each line and hard against the top, so a number reads as
     * belonging to the step that starts there.
     *
     * A FLAT `inkDim`, and not a translucent near-white. A step at full depth
     * puts its white hull line straight through this band, so the number is
     * as often on near-white as on the ground -- and anything translucent and
     * pale lifts INTO the hull and disappears. A dark violet is the one
     * colour that holds against both: faint on the ground, and still legible
     * where it crosses the curve.
     */
    for (int i = 0; i < count; ++i)
        drawTracked (g, juce::String (i + 1), f, colour::inkDim,
                     { plot.getX() + w * (float) i + 2.0f,
                       plot.getY() + f.getHeight() },
                     font::trackHint);
}

void Curve::build (const std::vector<float>& lo, const std::vector<float>& hi)
{
    under.clear();
    band .clear();
    hull .clear();

    const int n = (int) juce::jmin (lo.size(), hi.size());
    if (n <= 0) return;

    /* Unit space: x across 0..1, y DOWNWARDS from 0 at the top so that a
     * level of 1 is y = 0 and the transform in draw() is a plain scale. */
    auto ux = [n] (int i) { return n > 1 ? (float) i / (float) (n - 1) : 0.0f; };
    auto uy = [] (float level) { return 1.0f - juce::jlimit (0.0f, 1.0f, level); };

    hull.startNewSubPath (ux (0), uy (hi[0]));
    for (int i = 1; i < n; ++i) hull.lineTo (ux (i), uy (hi[(size_t) i]));
    for (int i = n - 1; i >= 0; --i) hull.lineTo (ux (i), uy (lo[(size_t) i]));

    /*
     * THE FILL IS BUILT FROM THE TOP EDGE ALONE, not from the hull.
     *
     * It used to be `under = hull` plus a close along the bottom -- but the
     * hull ends where its BACKWARD walk along `lo` finishes, at the LEFT
     * edge, so closing it to the bottom-RIGHT drew a diagonal across the
     * whole plot. Whenever the curve starts at zero that diagonal lies flat
     * along the baseline and is invisible, which is why it survived: set
     * Attack to 0, the envelope opens at 1.0 on its first sample, and the
     * same line becomes a triangle from the top-left corner.
     */
    under.startNewSubPath (ux (0), uy (hi[0]));
    for (int i = 1; i < n; ++i) under.lineTo (ux (i), uy (hi[(size_t) i]));
    under.lineTo (ux (n - 1), 1.0f);
    under.lineTo (ux (0), 1.0f);
    under.closeSubPath();

    /* Only where decimation actually kept a range is there a band to fill;
     * where lo == hi this stays empty and the drawing is a plain line, which
     * is what makes a one-step envelope look exactly as it always did. */
    bool spread = false;
    for (int i = 0; i < n && ! spread; ++i)
        spread = (hi[(size_t) i] - lo[(size_t) i]) > 1.0e-4f;

    if (spread)
    {
        band = hull;
        band.closeSubPath();
    }
}

void Curve::drawOutline (juce::Graphics& g, juce::Rectangle<float> plot, float floorLevel,
                         juce::Colour c, float thickness) const
{
    if (hull.isEmpty()) return;
    const float f = juce::jlimit (0.0f, 1.0f, floorLevel);
    const auto t = juce::AffineTransform::scale (plot.getWidth(), plot.getHeight() * (1.0f - f))
                     .translated (plot.getX(), plot.getY());
    g.setColour (c);
    g.strokePath (hull, juce::PathStrokeType (thickness), t);
}

void Curve::draw (juce::Graphics& g, juce::Rectangle<float> plot, float floorLevel) const
{
    if (hull.isEmpty()) return;

    const float f = juce::jlimit (0.0f, 1.0f, floorLevel);
    const float h = plot.getHeight() * (1.0f - f);

    /* m = floor + (1 - floor)*g is affine in g, so the floor is a squash of
     * the unit box onto the part of the plot above it -- exact, and it costs
     * no rebuild when Amount moves. */
    const auto t = juce::AffineTransform::scale (plot.getWidth(), h)
                     .translated (plot.getX(), plot.getY());

    if (f > 0.0f)
    {
        /* Everything below the floor is lit whatever the gate does. */
        g.setColour (colour::uvGlow);
        g.fillRect (plot.withTop (plot.getY() + h));
    }

    g.setColour (colour::uvGlow);
    g.fillPath (under, t);

    if (! band.isEmpty())
    {
        g.setColour (colour::uv);
        g.fillPath (band, t);
    }

    g.setColour (colour::uv);
    g.strokePath (hull, juce::PathStrokeType (stroke::rail), t);
}

}
