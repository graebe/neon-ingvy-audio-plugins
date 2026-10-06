// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The window ground at rest. See UvGround.h.
 */
#include "UvGround.h"

#include "UvTokens.h"

#include <NiUiAssets.h>

#include <cmath>

namespace uv::ground
{

namespace
{
/* The dot pitch, which tokens.json states as space-3 and ground.js as its
 * pitch; the generator checks they agree on the colours, and this checks the
 * pitch divides the tile. */
constexpr int pitch = (int) tok::space::space3;
static_assert (tileSize % pitch == 0, "the grain tile must repeat the dot paper exactly");

/* radial-gradient(circle, bg-dot 1px, transparent 1.2px), sampled at a
 * distance from the dot's centre: the dot's coverage there. */
float dotCoverage (float distance)
{
    constexpr float solid = 1.0f, clear = 1.2f;
    if (distance <= solid) return 1.0f;
    if (distance >= clear) return 0.0f;
    return (clear - distance) / (clear - solid);
}

juce::Image build()
{
    const auto grain = juce::ImageFileFormat::loadFrom (ni_ui_assets::groundgrain_png,
                                                        (size_t) ni_ui_assets::groundgrain_pngSize);
    /* scripts/gen-tokens.mjs refuses any other size. */
    jassert (grain.getWidth() == tileSize && grain.getHeight() == tileSize);

    const auto ground = tok::colour::bg000;
    const auto dot = tok::colour::bgDot;
    const auto noise = tok::colour::uvDeep;

    juce::Image tile (juce::Image::ARGB, tileSize, tileSize, false, juce::SoftwareImageType());
    juce::Image::BitmapData out (tile, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < tileSize; ++y)
    {
        for (int x = 0; x < tileSize; ++x)
        {
            /* The nearest dot: the dots sit on the multiples of the pitch, and
             * a pixel is sampled at its centre. */
            const auto offset = [] (int p)
            {
                const float m = std::fmod ((float) p + 0.5f, (float) pitch);
                return juce::jmin (m, (float) pitch - m);
            };
            const float d = std::hypot (offset (x), offset (y));

            auto c = ground.interpolatedWith (dot, dotCoverage (d));

            /* The grain over it, in the token's colour at the file's alpha. */
            if (grain.isValid())
                c = c.interpolatedWith (noise, grain.getPixelAt (x, y).getFloatAlpha());

            out.setPixelColour (x, y, c);
        }
    }

    return tile;
}

/* Kept until JUCE shuts down, as everything the kit caches is (UvType.h). */
class TileStore final : private juce::DeletedAtShutdown
{
public:
    ~TileStore() override { clearSingletonInstance(); }

    const juce::Image image = build();

    JUCE_DECLARE_SINGLETON_INLINE (TileStore, false)
};
} // namespace

const juce::Image& tile()
{
    JUCE_ASSERT_MESSAGE_THREAD
    return TileStore::getInstance()->image;
}

void paint (juce::Graphics& g, juce::Rectangle<float> area, juce::Point<float> origin)
{
    juce::Graphics::ScopedSaveState state (g);
    g.setTiledImageFill (tile(), juce::roundToInt (origin.x), juce::roundToInt (origin.y), 1.0f);
    g.fillRect (area);
}

} // namespace uv::ground
