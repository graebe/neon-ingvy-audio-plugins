// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The system's light. UvLight.h says what each glow is, and why two of them
 * blur twice as wide as the third.
 */
#include "UvLight.h"

#include <cmath>
#include <map>
#include <tuple>
#include <vector>

namespace uv::light
{

namespace
{
/* ================================================================ blur == */

/*
 * One box blur along a line: each output the mean of `size` inputs, the first
 * of them `left` before it. Outside the line is transparent, as an SVG
 * filter's edge is.
 */
void boxLine (const float* in, float* out, int n, int size, int left)
{
    double sum = 0.0;
    for (int k = -left; k < size - left; ++k)
        if (k >= 0 && k < n)
            sum += in[k];

    for (int i = 0; i < n; ++i)
    {
        out[i] = (float) (sum / (double) size);
        const int drop = i - left, add = i - left + size;
        if (drop >= 0 && drop < n) sum -= in[drop];
        if (add >= 0 && add < n)   sum += in[add];
    }
}

/*
 * A Gaussian of `sigma` along one line, in place, the way SVG's
 * feGaussianBlur specifies it and browsers run it: for sigma of 2 or more,
 * three box blurs of d = floor(sigma * 3 * sqrt(2 pi) / 4 + 0.5) -- all
 * centred for an odd d; for an even one, two of d centred half a pixel left
 * and right and one of d + 1 centred. Below 2, where the boxes would be too
 * coarse, the kernel itself.
 */
void gaussianLine (float* line, std::vector<float>& scratch, int n, float sigma)
{
    if (sigma <= 0.0f || n == 0)
        return;

    scratch.resize ((size_t) n);
    float* tmp = scratch.data();

    if (sigma >= 2.0f)
    {
        const int d = (int) std::floor (sigma * 3.0f * std::sqrt (2.0f * juce::MathConstants<float>::pi) / 4.0f + 0.5f);
        if (d % 2 == 1)
        {
            boxLine (line, tmp, n, d, d / 2);
            boxLine (tmp, line, n, d, d / 2);
            boxLine (line, tmp, n, d, d / 2);
        }
        else
        {
            boxLine (line, tmp, n, d, d / 2);
            boxLine (tmp, line, n, d, d / 2 - 1);
            boxLine (line, tmp, n, d + 1, d / 2);
        }
        std::copy (tmp, tmp + n, line);
        return;
    }

    const int radius = (int) std::ceil (3.0f * sigma);
    std::vector<float> kernel ((size_t) (2 * radius + 1));
    float total = 0.0f;
    for (int i = -radius; i <= radius; ++i)
        total += kernel[(size_t) (i + radius)] = std::exp (-(float) (i * i) / (2.0f * sigma * sigma));

    for (int i = 0; i < n; ++i)
    {
        float acc = 0.0f;
        for (int k = -radius; k <= radius; ++k)
            if (i + k >= 0 && i + k < n)
                acc += line[i + k] * kernel[(size_t) (k + radius)];
        tmp[i] = acc / total;
    }
    std::copy (tmp, tmp + n, line);
}

/* Coverage, 0..1, at device resolution over `area` (in g's units). */
struct Field
{
    juce::Rectangle<float> area;
    float scale = 1.0f;
    int w = 0, h = 0;
    std::vector<float> a;
};

/* Device pixels per unit of `g`, so a halo is computed at the resolution it
 * is shown at rather than scaled up. */
float deviceScale (juce::Graphics& g)
{
    return juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
}

Field rasterise (const juce::Path& shape, float margin, float scale)
{
    Field f;
    f.area = shape.getBounds().expanded (margin);
    f.scale = scale;
    f.w = juce::jmax (1, (int) std::ceil (f.area.getWidth() * scale));
    f.h = juce::jmax (1, (int) std::ceil (f.area.getHeight() * scale));

    juce::Image mask (juce::Image::SingleChannel, f.w, f.h, true, juce::SoftwareImageType());
    {
        /* Any opaque colour: a single-channel image keeps only coverage. */
        juce::Graphics mg (mask);
        mg.setColour (juce::Colour().withAlpha (1.0f));
        mg.fillPath (shape, juce::AffineTransform::translation (-f.area.getX(), -f.area.getY()).scaled (scale));
    }

    f.a.resize ((size_t) (f.w * f.h));
    const juce::Image::BitmapData data (mask, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x)
            f.a[(size_t) (y * f.w + x)] = (float) *data.getPixelPointer (x, y) / 255.0f;
    return f;
}

/* A Gaussian of `sigma` (in g's units) over the whole field. */
void blur (Field& f, float sigma)
{
    const float s = sigma * f.scale;
    std::vector<float> scratch, column ((size_t) f.h);

    for (int y = 0; y < f.h; ++y)
        gaussianLine (f.a.data() + y * f.w, scratch, f.w, s);

    for (int x = 0; x < f.w; ++x)
    {
        for (int y = 0; y < f.h; ++y) column[(size_t) y] = f.a[(size_t) (y * f.w + x)];
        gaussianLine (column.data(), scratch, f.h, s);
        for (int y = 0; y < f.h; ++y) f.a[(size_t) (y * f.w + x)] = column[(size_t) y];
    }
}

juce::Image toMask (const Field& f, float opacity)
{
    juce::Image mask (juce::Image::SingleChannel, f.w, f.h, true, juce::SoftwareImageType());
    juce::Image::BitmapData data (mask, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x)
            *data.getPixelPointer (x, y) = (juce::uint8) juce::jlimit (0, 255,
                juce::roundToInt (255.0f * opacity * f.a[(size_t) (y * f.w + x)]));
    return mask;
}

/* Draws a coverage mask, `scale` mask pixels to the unit, at `origin`, in
 * `colour`. */
void drawMask (juce::Graphics& g, const juce::Image& mask, juce::Point<float> origin, float scale,
               juce::Colour colour)
{
    g.setColour (colour);
    g.drawImageTransformed (mask, juce::AffineTransform::scale (1.0f / scale).translated (origin), true);
}

/* =========================================================== glow-led == */

/*
 * glow-led as tokens.css applies it: filter: drop-shadow(10px uv-deep)
 * drop-shadow(2px uv), each at ledOpacity. The second filter shadows the
 * first one's RESULT -- the element and its violet halo -- so its white lies
 * under both. The two masks, white then violet, are what the element is
 * painted over.
 */
struct LedMasks
{
    juce::Image white, violet;
    juce::Point<float> offset;   // from the element's bounds to the masks'
    float scale = 1.0f;
};

LedMasks ledMasks (const juce::Path& element, float scale)
{
    const auto& wide = tok::shadow::glowLed[0];    // 10px uv-deep
    const auto& tight = tok::shadow::glowLed[1];   // 2px uv
    const float margin = std::ceil (3.0f * (wide.blur + tight.blur)) + 2.0f;

    const auto m = rasterise (element, margin, scale);

    auto violet = m;
    blur (violet, wide.blur);

    auto white = m;
    for (size_t i = 0; i < white.a.size(); ++i)
        white.a[i] = m.a[i] + (1.0f - m.a[i]) * ledOpacity * violet.a[i];
    blur (white, tight.blur);

    return { toMask (white, ledOpacity), toMask (violet, ledOpacity),
             m.area.getPosition() - element.getBounds().getPosition(), scale };
}

void drawLed (juce::Graphics& g, const LedMasks& masks, juce::Point<float> elementOrigin)
{
    const auto origin = elementOrigin + masks.offset;
    drawMask (g, masks.white, origin, masks.scale, juce::Colour (tok::shadow::glowLed[1].argb));
    drawMask (g, masks.violet, origin, masks.scale, juce::Colour (tok::shadow::glowLed[0].argb));
}

/* ========================================================= box-shadow == */

/*
 * The coverage of a blurred interval: the share of a Gaussian of `sigma`,
 * centred at x, that falls inside [a, b]. A blurred rectangle is the product
 * of this along each axis -- exactly -- so a rectangle's box-shadow needs no
 * blur pass at all.
 */
float blurredInterval (float x, float a, float b, float sigma)
{
    const float k = 1.0f / (sigma * std::sqrt (2.0f));
    return 0.5f * (std::erf ((b - x) * k) - std::erf ((a - x) * k));
}

/* Three standard deviations hold all but 0.3 % of a Gaussian. */
float reachOf (float sigma) { return std::ceil (3.0f * sigma) + 1.0f; }

/* A blurred rectangle's mask, cached: a window has a few sizes of lit thing
 * and redraws them often, and the mask depends only on the size, the blur and
 * the scale. Keys are quantised to an eighth of a pixel. */
juce::Image rectMask (float w, float h, float sigma, float scale)
{
    using Key = std::tuple<int, int, int, int>;
    static std::map<Key, juce::Image> cache;

    const auto q = [] (float v) { return juce::roundToInt (v * 8.0f); };
    const Key key { q (w), q (h), q (sigma), q (scale) };
    if (const auto it = cache.find (key); it != cache.end())
        return it->second;

    const float reach = reachOf (sigma);
    const int iw = juce::jmax (1, (int) std::ceil ((w + 2.0f * reach) * scale));
    const int ih = juce::jmax (1, (int) std::ceil ((h + 2.0f * reach) * scale));

    std::vector<float> px ((size_t) iw), py ((size_t) ih);
    for (int i = 0; i < iw; ++i)
        px[(size_t) i] = blurredInterval (((float) i + 0.5f) / scale - reach, 0.0f, w, sigma);
    for (int j = 0; j < ih; ++j)
        py[(size_t) j] = blurredInterval (((float) j + 0.5f) / scale - reach, 0.0f, h, sigma);

    juce::Image mask (juce::Image::SingleChannel, iw, ih, true, juce::SoftwareImageType());
    {
        juce::Image::BitmapData data (mask, juce::Image::BitmapData::writeOnly);
        for (int j = 0; j < ih; ++j)
            for (int i = 0; i < iw; ++i)
                *data.getPixelPointer (i, j) =
                    (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (255.0f * px[(size_t) i] * py[(size_t) j]));
    }

    if (cache.size() > 64)   // past this, sizes are being computed per frame
        cache.clear();
    cache.emplace (key, mask);
    return mask;
}

/* Everything outside `shape`: a clip that keeps a box-shadow off its box. */
juce::Path outside (const juce::Graphics& g, const juce::Path& shape)
{
    juce::Path p;
    p.addRectangle (g.getClipBounds().toFloat().getUnion (shape.getBounds()).expanded (64.0f));
    p.addPath (shape);
    p.setUsingNonZeroWinding (false);
    return p;
}
} // namespace

/* =============================================================== public == */

void shadow (juce::Graphics& g, juce::Rectangle<float> shape, const tok::ShadowLayer& layer,
             float opacity)
{
    const auto box = shape.translated (layer.x, layer.y).expanded (layer.spread);
    if (box.isEmpty())
        return;

    const auto colour = juce::Colour (layer.argb).withMultipliedAlpha (opacity);

    if (layer.blur <= 0.0f)
    {
        g.setColour (colour);
        g.fillRect (box);
        return;
    }

    /* box-shadow: the blur is a radius, a Gaussian of half of it. */
    const float sigma = layer.blur * 0.5f;
    const float scale = deviceScale (g);
    const float reach = reachOf (sigma);
    drawMask (g, rectMask (box.getWidth(), box.getHeight(), sigma, scale),
              box.getPosition() - juce::Point<float> (reach, reach), scale, colour);
}

void shadow (juce::Graphics& g, const juce::Path& shape, const tok::ShadowLayer& layer,
             float opacity)
{
    /* A spread is a dilation, which only a rectangle has exactly; nothing in
     * the system spreads a path. */
    jassert (layer.spread == 0.0f);

    const auto colour = juce::Colour (layer.argb).withMultipliedAlpha (opacity);
    const auto moved = juce::AffineTransform::translation (layer.x, layer.y);

    if (layer.blur <= 0.0f)
    {
        g.setColour (colour);
        g.fillPath (shape, moved);
        return;
    }

    const float sigma = layer.blur * 0.5f;
    juce::Path p (shape);
    p.applyTransform (moved);
    auto f = rasterise (p, reachOf (sigma), deviceScale (g));
    blur (f, sigma);
    drawMask (g, toMask (f, 1.0f), f.area.getPosition(), f.scale, colour);
}

void dropShadow (juce::Graphics& g, const juce::Path& shape, const tok::ShadowLayer& layer,
                 float opacity)
{
    jassert (layer.spread == 0.0f);   // a drop-shadow() has none

    const auto colour = juce::Colour (layer.argb).withMultipliedAlpha (opacity);
    juce::Path p (shape);
    p.applyTransform (juce::AffineTransform::translation (layer.x, layer.y));

    /* drop-shadow(): the length is the standard deviation itself. */
    auto f = rasterise (p, reachOf (layer.blur), deviceScale (g));
    blur (f, layer.blur);
    drawMask (g, toMask (f, 1.0f), f.area.getPosition(), f.scale, colour);
}

void glowLed (juce::Graphics& g, juce::Rectangle<float> element)
{
    if (element.isEmpty())
        return;

    /* Kept per size and scale: a grid of lit steps is one size. */
    using Key = std::tuple<int, int, int>;
    static std::map<Key, LedMasks> cache;

    const float scale = deviceScale (g);
    const auto q = [] (float v) { return juce::roundToInt (v * 8.0f); };
    const Key key { q (element.getWidth()), q (element.getHeight()), q (scale) };

    auto it = cache.find (key);
    if (it == cache.end())
    {
        if (cache.size() > 64)
            cache.clear();
        juce::Path shape;
        shape.addRectangle (element.withPosition (0.0f, 0.0f));
        it = cache.emplace (key, ledMasks (shape, scale)).first;
    }
    drawLed (g, it->second, element.getPosition());
}

void glowLed (juce::Graphics& g, const juce::Path& element)
{
    if (element.isEmpty())
        return;
    drawLed (g, ledMasks (element, deviceScale (g)), element.getBounds().getPosition());
}

void glowFocus (juce::Graphics& g, juce::Rectangle<float> element, float cornerRadius)
{
    juce::Path shape;
    if (cornerRadius > 0.0f)
        shape.addRoundedRectangle (element, cornerRadius);
    else
        shape.addRectangle (element);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (outside (g, shape));

    /* Last layer first (the halo), then the ring on top: the ring is the
     * shape grown by its spread, concentric with a rounded one. */
    const auto& ring = tok::shadow::glowFocus[0];
    const auto& halo = tok::shadow::glowFocus[1];

    if (cornerRadius > 0.0f)
    {
        shadow (g, shape, halo);
        juce::Path grown;
        grown.addRoundedRectangle (element.expanded (ring.spread), cornerRadius + ring.spread);
        g.setColour (juce::Colour (ring.argb));
        g.fillPath (grown);
    }
    else
    {
        shadow (g, element, halo);
        shadow (g, element, ring);
    }
}

void glowArc (juce::Graphics& g, const juce::Path& stroke)
{
    dropShadow (g, stroke, { 0.0f, 0.0f, arcDeviation, 0.0f, tok::argb::ultraviolet::uvDeep }, arcOpacity);
}

} // namespace uv::light
