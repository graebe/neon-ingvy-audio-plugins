// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The system's light. See UvLight.h for what each glow is and why it is drawn
 * this way.
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
/* A CSS blur radius is a Gaussian of standard deviation r / 2. */
float sigmaOf (float blur) { return blur * 0.5f; }

/* Three standard deviations hold all but 0.3 % of a Gaussian: past them an
 * 8-bit alpha is zero. */
float reachOf (float sigma) { return std::ceil (3.0f * sigma) + 1.0f; }

/* Device pixels per unit of `g`, so a halo is computed at the resolution it
 * is shown at rather than scaled up. */
float deviceScale (juce::Graphics& g)
{
    return juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
}

/*
 * The coverage of a blurred interval: the share of a Gaussian of `sigma`,
 * centred at x, that falls inside [a, b]. A blurred rectangle is the product
 * of this along each axis -- exactly, which is why a rectangle's glow needs
 * no blur pass at all.
 */
float blurredInterval (float x, float a, float b, float sigma)
{
    const float k = 1.0f / (sigma * std::sqrt (2.0f));
    return 0.5f * (std::erf ((b - x) * k) - std::erf ((a - x) * k));
}

/* Draws a single-channel mask, `scale` mask pixels to the unit, at `origin`
 * in the current colour. */
void drawMask (juce::Graphics& g, const juce::Image& mask, juce::Point<float> origin, float scale)
{
    g.drawImageTransformed (mask,
                            juce::AffineTransform::scale (1.0f / scale).translated (origin),
                            true);
}

/*
 * A blurred rectangle's mask, cached: a window has a few sizes of lit thing
 * (a step, a tab, a button) and redraws them often, and the mask depends only
 * on the size, the blur and the scale. Keys are quantised to an eighth of a
 * pixel. Message thread, as all painting is.
 */
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

    /* One profile per axis; the mask is their product. */
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

    /* A cache that grew past this would mean sizes computed per frame. */
    if (cache.size() > 64)
        cache.clear();
    cache.emplace (key, mask);
    return mask;
}

/* A separable Gaussian over a single-channel image, in place. */
void blurMask (juce::Image& mask, float sigma)
{
    if (sigma <= 0.0f)
        return;

    const int radius = (int) std::ceil (3.0f * sigma);
    std::vector<float> kernel ((size_t) (2 * radius + 1));
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i)
    {
        const float v = std::exp (-(float) (i * i) / (2.0f * sigma * sigma));
        kernel[(size_t) (i + radius)] = v;
        sum += v;
    }
    for (auto& v : kernel)
        v /= sum;

    const int w = mask.getWidth(), h = mask.getHeight();
    std::vector<float> a ((size_t) (w * h)), b ((size_t) (w * h));

    juce::Image::BitmapData data (mask, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            a[(size_t) (y * w + x)] = (float) *data.getPixelPointer (x, y);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k)
            {
                const int xx = x + k;
                if (xx >= 0 && xx < w)
                    acc += a[(size_t) (y * w + xx)] * kernel[(size_t) (k + radius)];
            }
            b[(size_t) (y * w + x)] = acc;
        }

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k)
            {
                const int yy = y + k;
                if (yy >= 0 && yy < h)
                    acc += b[(size_t) (yy * w + x)] * kernel[(size_t) (k + radius)];
            }
            *data.getPixelPointer (x, y) = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (acc));
        }
}

juce::Colour colourOf (const tok::ShadowLayer& layer, float opacity)
{
    return juce::Colour (layer.argb).withMultipliedAlpha (opacity);
}

/* The layers of a CSS shadow are painted last first: the first is on top. */
template <size_t N, typename Shape>
void layers (juce::Graphics& g, const Shape& shape, const tok::ShadowLayer (&list)[N], float opacity)
{
    for (size_t i = N; i-- > 0;)
        shadow (g, shape, list[i], opacity);
}

/* Everything outside `shape`: a clip that keeps a box-shadow off the box. */
juce::Path outside (const juce::Graphics& g, const juce::Path& shape)
{
    juce::Path p;
    p.addRectangle (g.getClipBounds().toFloat().getUnion (shape.getBounds()).expanded (64.0f));
    p.addPath (shape);
    p.setUsingNonZeroWinding (false);
    return p;
}
} // namespace

void shadow (juce::Graphics& g, juce::Rectangle<float> shape, const tok::ShadowLayer& layer,
             float opacity)
{
    const auto box = shape.translated (layer.x, layer.y).expanded (layer.spread);
    if (box.isEmpty())
        return;

    const auto colour = colourOf (layer, opacity);

    if (layer.blur <= 0.0f)
    {
        g.setColour (colour);
        g.fillRect (box);
        return;
    }

    const float sigma = sigmaOf (layer.blur);
    const float scale = deviceScale (g);
    const auto mask = rectMask (box.getWidth(), box.getHeight(), sigma, scale);
    const float reach = reachOf (sigma);

    g.setColour (colour);
    drawMask (g, mask, box.getPosition() - juce::Point<float> (reach, reach), scale);
}

void shadow (juce::Graphics& g, const juce::Path& shape, const tok::ShadowLayer& layer,
             float opacity)
{
    /* A spread past the shape is a dilation, which only a rectangle has
     * exactly; an outline would need a stroke here, and nothing in the system
     * spreads a path. */
    jassert (layer.spread == 0.0f);

    const auto bounds = shape.getBounds().translated (layer.x, layer.y);
    if (bounds.isEmpty())
        return;

    const auto colour = colourOf (layer, opacity);

    if (layer.blur <= 0.0f)
    {
        g.setColour (colour);
        g.fillPath (shape, juce::AffineTransform::translation (layer.x, layer.y));
        return;
    }

    const float sigma = sigmaOf (layer.blur);
    const float scale = deviceScale (g);
    const float reach = reachOf (sigma);
    const auto area = bounds.expanded (reach);

    juce::Image mask (juce::Image::SingleChannel,
                      juce::jmax (1, (int) std::ceil (area.getWidth() * scale)),
                      juce::jmax (1, (int) std::ceil (area.getHeight() * scale)),
                      true, juce::SoftwareImageType());
    {
        /* Any opaque colour: a single-channel image keeps only coverage. */
        juce::Graphics mg (mask);
        mg.setColour (juce::Colour().withAlpha (1.0f));
        mg.fillPath (shape, juce::AffineTransform::translation (layer.x - area.getX(), layer.y - area.getY())
                                .scaled (scale));
    }
    blurMask (mask, sigma * scale);

    g.setColour (colour);
    drawMask (g, mask, area.getPosition(), scale);
}

void glowLed (juce::Graphics& g, juce::Rectangle<float> element)
{
    layers (g, element, tok::shadow::glowLed, ledOpacity);
}

void glowLed (juce::Graphics& g, const juce::Path& element)
{
    layers (g, element, tok::shadow::glowLed, ledOpacity);
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

    /* Last layer first (the halo), then the ring on top. The ring is the
     * shape grown by its spread; with a corner it stays concentric. */
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
    const tok::ShadowLayer arc { 0.0f, 0.0f, arcBlur, 0.0f, tok::argb::ultraviolet::uvDeep };
    shadow (g, stroke, arc, arcOpacity);
}

} // namespace uv::light
