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

        const float a0 = -juce::MathConstants<float>::halfPi
                       + (float) i * slot + (slot - fill) * 0.5f;

        juce::Path p;
        p.addCentredArc (c.x, c.y, rSeg, rSeg, 0.0f, a0, a0 + fill, true);
        /* unlit ink-dim, active phosphor, the current step ink */
        g.setColour (at ? colour::ink : on ? colour::phosphor : colour::inkDim);
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
