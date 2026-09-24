#include "Uv.h"

namespace uv
{

namespace font
{
Faces* faces = nullptr;

static juce::Font make (const juce::Typeface::Ptr& face, float height)
{
    /* FontOptions is the JUCE 8 road; the deprecated Font(height, style)
     * constructors do not take a typeface without also taking a name. */
    if (face != nullptr)
        return juce::Font (juce::FontOptions (face).withHeight (height));

    /* The system's fallback chain, for a host that somehow loads the plugin
     * without its own resources. Named rather than left to the default sans,
     * because a proportional face in a 64px readout column wraps. */
    return juce::Font (juce::FontOptions ("Menlo", height, juce::Font::plain));
}

static juce::Typeface::Ptr reg() { return faces != nullptr ? faces->regular : nullptr; }
static juce::Typeface::Ptr med() { return faces != nullptr ? faces->medium  : nullptr; }

juce::Font readout() { return make (med(), 28.0f); }
juce::Font title()   { return make (med(), 12.0f); }
juce::Font label()   { return make (med(), 11.0f); }
juce::Font value()   { return make (reg(), 13.0f); }
juce::Font button()  { return make (med(), 12.0f); }
juce::Font hint()    { return make (reg(), 10.0f); }
}

/*
 * TRACKING, GLYPH BY GLYPH.
 *
 * juce::Font has no letter-spacing, and three of the system's six styles are
 * tracked -- the uppercase ones, where it is doing real work: 11px uppercase
 * mono at .1em is legible as a label, and the same text untracked reads as a
 * word someone forgot to lowercase.
 *
 * Monospace makes this cheap: every advance is the same, so the width is
 * arithmetic rather than a shaping pass.
 */
float drawTracked (juce::Graphics& g, const juce::String& text, const juce::Font& f,
                   juce::Colour c, juce::Point<float> origin, float tracking)
{
    if (text.isEmpty()) return 0.0f;

    g.setFont (f);
    g.setColour (c);

    const float extra = f.getHeight() * tracking;
    float x = origin.x;

    for (auto ch : text)
    {
        const juce::String glyph (juce::String::charToString (ch));
        g.drawSingleLineText (glyph, juce::roundToInt (x),
                              juce::roundToInt (origin.y));
        x += juce::GlyphArrangement::getStringWidth (f, glyph) + extra;
    }
    return x - origin.x - extra;   /* the trailing gap is not part of the text */
}

float trackedWidth (const juce::String& text, const juce::Font& f, float tracking)
{
    if (text.isEmpty()) return 0.0f;
    return juce::GlyphArrangement::getStringWidth (f, text)
         + f.getHeight() * tracking * (float) (text.length() - 1);
}

/*
 * THE TWO GLOWS.
 *
 * A CSS box-shadow blur has no direct equivalent in a JUCE paint routine, and
 * the honest alternatives -- render to an image and blur it, or use a
 * DropShadow -- cost a buffer per control per frame for a halo six pixels
 * across. Concentric strokes of falling alpha land in the same place at a
 * fraction of the cost, and at these radii the difference is not visible.
 */
static void halo (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c,
                  float radius, float cornerRadius, float alpha)
{
    for (float i = radius; i >= 1.0f; i -= 1.0f)
    {
        /* Falls off with the square of the distance, as a blur does. */
        const float t = 1.0f - (i / radius);
        g.setColour (c.withAlpha (alpha * t * t));
        const auto ring = r.expanded (i);
        if (cornerRadius > 0.0f) g.drawRoundedRectangle (ring, cornerRadius + i, 1.0f);
        else                     g.drawRect (ring, 1.0f);
    }
}

/*
 * glow-led: 0 0 10px uvDeep, 0 0 2px uv.
 *
 * TWO HALOES, AND THE ORDER IS THE POINT. The wide one is the SATURATED
 * violet -- that is where the hue lives now that the fill is near white --
 * and the tight one is the core colour, which keeps the element's own edge
 * from being eaten by the violet. One halo in `uv` alone, which is what this
 * used to be, leaves a white element with a white glow: no cast at all.
 */
void glowLed (juce::Graphics& g, juce::Rectangle<float> r, float cornerRadius)
{
    halo (g, r, colour::uvDeep, 10.0f, cornerRadius, 0.55f);
    halo (g, r, colour::uv,      2.0f, cornerRadius, 0.55f);
}

/* glow-focus: 0 0 0 1px uv, 0 0 8px uvGlow -- a hard core ring, then a
 * violet bloom. uvGlow is uvDeep at half alpha, so the halo is drawn in the
 * deep tone and the ring in the bright one, as with glow-led. */
void glowFocus (juce::Graphics& g, juce::Rectangle<float> r, float cornerRadius)
{
    g.setColour (colour::uv);
    if (cornerRadius > 0.0f) g.drawRoundedRectangle (r.expanded (0.5f), cornerRadius, 1.0f);
    else                     g.drawRect (r.expanded (0.5f), 1.0f);
    halo (g, r, colour::uvDeep, 8.0f, cornerRadius, 0.50f);
}

}
