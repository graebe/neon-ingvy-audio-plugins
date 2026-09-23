#include "PhosphorComponents.h"

using namespace phosphor;

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

    /* The preview's radii as fractions of 240, so the 120px variant is the
     * same drawing at half size. */
    const float rTrack = d * (99.0f  / 240.0f);
    const float rSeg   = d * (108.0f / 240.0f);
    const int   n      = juce::jmax (1, model.length);

    g.setColour (colour::line100);
    g.drawEllipse (juce::Rectangle<float> (rTrack * 2.0f, rTrack * 2.0f).withCentre (c),
                   stroke::hair);

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
    const float seg = juce::jlimit (2.0f, 12.0f, 300.0f / (float) n);
    const float slot = juce::MathConstants<float>::twoPi / (float) n;
    const float fill = slot * 0.8f;             /* 12 degrees of 15 in the preview */

    for (int i = 0; i < n; ++i)
    {
        const bool on = model.stepOn && model.stepOn (i);
        const bool at = model.moving && i == model.playhead;

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

        juce::Path p;
        p.addCentredArc (c.x, c.y, rSeg, rSeg, 0.0f, a0, a0 + fill, true);
        /*
         * WHETHER A STEP IS ON DECIDES THE COLOUR; THE PLAYHEAD ONLY PICKS
         * THE SHADE. Testing `at` first let the playhead paint an OFF step in
         * ink -- brighter than a lit step's phosphor -- so for one segment
         * per revolution the ring reported a step that is not in the pattern.
         * StepGridView already resolves it this way round (a gap under the
         * playhead gets a glow wash, never phosphor).
         */
        g.setColour (on ? (at ? colour::ink       : colour::phosphor)
                        : (at ? colour::phosphorGlow : colour::inkDim));
        g.strokePath (p, juce::PathStrokeType (seg, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::butt));
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

        if (at) glowLed (g, r);

        if (tie)
        {
            /* A hollow phosphor outline with a bar across it -- the tie says
             * "this step holds through", so it is not a fill. */
            g.setColour (colour::bg200);
            g.fillRect (r);
            g.setColour (colour::phosphor);
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
            g.setColour (colour::phosphor);
            g.fillRect (r.withTop (r.getBottom() - h));
            g.setColour (colour::phosphor);
            g.drawRect (r, stroke::hair);
        }
        else
        {
            g.setColour (at ? colour::bg300 : colour::bg200);
            g.fillRect (r);
            /* The playhead on an OFF step is a phosphor-glow wash: the step
             * is not on, so it must not be phosphor. */
            if (at)
            {
                g.setColour (colour::phosphorGlow);
                g.fillRect (r);
            }
            /* Every fourth step carries a rail-coloured border, so bars read
             * without numbers. */
            g.setColour (at ? colour::phosphor : (i % 4 == 0 ? colour::line200 : colour::line100));
            g.drawRect (r, stroke::hair);
        }

        /* The cursor -- the step the knobs edit. Not a system state (its five
         * are off/on/hover/active/disabled), so it is drawn as the system
         * draws selection everywhere else: in phosphor, here as a mark
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

namespace phosphor::plot
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
        g.setColour (colour::phosphorGlow);
        g.fillRect (plot.withTop (plot.getY() + h));
    }

    g.setColour (colour::phosphorGlow);
    g.fillPath (under, t);

    if (! band.isEmpty())
    {
        g.setColour (colour::phosphor);
        g.fillPath (band, t);
    }

    g.setColour (colour::phosphor);
    g.strokePath (hull, juce::PathStrokeType (stroke::rail), t);
}

}
