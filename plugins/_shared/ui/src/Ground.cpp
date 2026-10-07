// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The animated window ground. Ground.h has the design and the rules,
 * GroundField.h the simulation.
 */
#include "Ground.h"

#include "ReducedMotion.h"
#include "UvGround.h"
#include "UvTokens.h"
#include "WaveSource.h"

#include <NiUiAssets.h>

#include <cmath>
#include <cstring>

namespace ni::ui
{

namespace p = uv::tok::motion::ground;
namespace c = uv::tok::colour;

namespace
{
constexpr int pitch = (int) p::pitch;
constexpr int half = pitch / 2;
/* The grain repeats every 8 dots: 8 x 8 grain positions a sprite can sit at. */
constexpr int grainCells = uv::ground::tileSize / pitch;
static_assert (grainCells * pitch == uv::ground::tileSize, "the grain must repeat the dot paper");

/* The dot is drawn at this alpha over the ground so that, at rest, it is
 * bg-dot exactly; a peak raises the alpha and so brightens it above bg-dot
 * with no second colour (ground.js, dotBase). */
constexpr float dotAlpha = 0.77f;
/* The static ground's dot edge: solid to its radius, clear 0.2px past it
 * (radial-gradient(circle, bg-dot 1px, transparent 1.2px)). */
constexpr float dotSoftEdge = 0.2f;

/* How much of a pixel `distance` from a dot's centre the dot covers. */
float dotCoverage (float distance, float radius)
{
    if (distance <= radius)
        return 1.0f;
    if (distance >= radius + dotSoftEdge)
        return 0.0f;
    return (radius + dotSoftEdge - distance) / dotSoftEdge;
}

/*
 * Every sprite, built once: `levels` rows of the 64 grain positions, each a
 * pitch square with its dot in the middle. A sprite at `mid` is the static
 * tile's own pixels, computed the same way (UvGround.cpp), so a field at rest
 * is the ground at rest.
 */
juce::Image buildAtlas()
{
    const auto grain = juce::ImageFileFormat::loadFrom (ni_ui_assets::groundgrain_png,
                                                        (size_t) ni_ui_assets::groundgrain_pngSize);
    jassert (grain.getWidth() == uv::ground::tileSize && grain.getHeight() == uv::ground::tileSize);

    juce::Image atlas (juce::Image::ARGB, grainCells * grainCells * pitch, ground::levels * pitch, false,
                       juce::SoftwareImageType());
    juce::Image::BitmapData out (atlas, juce::Image::BitmapData::writeOnly);

    const auto bg = c::bg000;
    for (int level = 0; level < ground::levels; ++level)
    {
        const float v = (float) (level - ground::mid) / (float) ground::mid;
        const float radius = p::dotR + p::dotDR * v;
        /* The dot's alpha over the ground, relative to rest: 1 is bg-dot. */
        const float dotGain = juce::jlimit (0.0f, 1.0f, dotAlpha * (1.0f + p::dotDA * v)) / dotAlpha;
        const auto dot = juce::Colour::fromFloatRGBA (
            bg.getFloatRed() + (c::bgDot.getFloatRed() - bg.getFloatRed()) * dotGain,
            bg.getFloatGreen() + (c::bgDot.getFloatGreen() - bg.getFloatGreen()) * dotGain,
            bg.getFloatBlue() + (c::bgDot.getFloatBlue() - bg.getFloatBlue()) * dotGain, 1.0f);
        const auto dotAtLevel = juce::exactlyEqual (dotGain, 1.0f) ? c::bgDot : dot;
        /* The grain's density relative to rest. */
        const float grainGain = juce::jlimit (p::grainMin, p::grainMax, p::grainBase * (1.0f + p::grainK * v)) / p::grainBase;

        for (int g = 0; g < grainCells * grainCells; ++g)
        {
            const int gi = g % grainCells, gj = g / grainCells;
            for (int y = 0; y < pitch; ++y)
                for (int x = 0; x < pitch; ++x)
                {
                    const float d = std::hypot ((float) (x - half) + 0.5f, (float) (y - half) + 0.5f);
                    auto col = bg.interpolatedWith (dotAtLevel, dotCoverage (d, radius));

                    const int tx = (gi * pitch - half + x + uv::ground::tileSize) % uv::ground::tileSize;
                    const int ty = (gj * pitch - half + y + uv::ground::tileSize) % uv::ground::tileSize;
                    if (grain.isValid())
                        col = col.interpolatedWith (c::uvDeep, juce::jmin (1.0f, grain.getPixelAt (tx, ty).getFloatAlpha() * grainGain));

                    out.setPixelColour (g * pitch + x, level * pitch + y, col);
                }
        }
    }
    return atlas;
}

/* Kept until JUCE shuts down, as everything the kit caches is (UvType.h). */
class AtlasStore final : private juce::DeletedAtShutdown
{
public:
    ~AtlasStore() override { clearSingletonInstance(); }

    const juce::Image image = buildAtlas();

    JUCE_DECLARE_SINGLETON_INLINE (AtlasStore, false)
};

const juce::Image& atlas()
{
    JUCE_ASSERT_MESSAGE_THREAD
    return AtlasStore::getInstance()->image;
}

/* Where dot (i, j)'s sprite at `level` is in the atlas. */
juce::Point<int> spriteAt (int level, int i, int j)
{
    const int g = (j % grainCells) * grainCells + (i % grainCells);
    return { g * pitch, level * pitch };
}
} // namespace

namespace ground
{
juce::Image sprite (int level, int i, int j)
{
    const auto at = spriteAt (juce::jlimit (0, levels - 1, level), i, j);
    return atlas().getClippedImage ({ at.x, at.y, pitch, pitch }).createCopy();
}
} // namespace ground

/* -------------------------------------------------------------- ground -- */

Ground::Ground (Clock c)
    : clock (c ? std::move (c) : Clock ([] { return juce::Time::getMillisecondCounterHiRes(); })),
      reducedQuery ([] { return systemReducesMotion(); })
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
    setWantsKeyboardFocus (false);
    /* Decoration: the beat it shows is one the user hears. */
    setAccessible (false);
}

Ground::~Ground()
{
    stopTimer();
}

void Ground::setRingSource (RingSource source)
{
    rings = std::move (source);
    updateTimer();
}

void Ground::setSourceRoot (juce::Component* root)
{
    sourceRoot = root;
    refreshWalls();
}

void Ground::setWalls (const std::vector<juce::Rectangle<float>>& walls)
{
    sourceRoot = nullptr;
    const bool wasMovingNow = field.isRunning();
    field.setWalls (walls);
    if (wasMovingNow && ! field.isRunning())
        restored();
}

void Ground::refreshWalls()
{
    if (sourceRoot == nullptr)
        return;
    const bool wasMovingNow = field.isRunning();
    field.setWalls (collectWaveSources (*sourceRoot, *this));
    if (wasMovingNow && ! field.isRunning())
        restored();
}

void Ground::setReducedMotionQuery (std::function<bool()> query)
{
    reducedQuery = query ? std::move (query) : std::function<bool()> ([] { return systemReducesMotion(); });
}

bool Ground::reduced() const
{
    return reducedQuery();
}

void Ground::trigger (float strength)
{
    if (! enabled || reduced())
        return;
    refreshWalls();
    const bool was = field.isRunning();
    field.trigger (strength);
    if (! was && field.isRunning())
        lastTick = clock();
    updateTimer();
}

void Ground::setEnabled (bool on)
{
    if (on == enabled)
        return;
    enabled = on;
    const bool was = field.isRunning();
    field.setEnabled (on);
    if (was && ! field.isRunning())
        restored();

    /* Rings counted while it was off are not played late. */
    if (on && rings)
        while (rings (ringBuffer.data(), (int) ringBuffer.size()) == (int) ringBuffer.size()) {}
    updateTimer();
}

void Ground::updateTimer()
{
    const bool run = enabled && (rings != nullptr || field.isRunning());
    if (run && ! isTimerRunning())
        startTimerHz ((int) p::fps);
    else if (! run && isTimerRunning())
        stopTimer();
}

void Ground::tick()
{
    const double now = clock();

    /* The rings that arrived since the last frame, all of them. */
    if (rings)
    {
        const int capacity = (int) ringBuffer.size();
        for (bool more = true; more;)
        {
            const int n = juce::jlimit (0, capacity, rings (ringBuffer.data(), capacity));
            more = n == capacity;
            if (n == 0 || ! enabled)
                continue;
            if (reduced())
                continue;
            refreshWalls();
            for (int k = 0; k < n; ++k)
            {
                const bool was = field.isRunning();
                field.trigger (ringBuffer[(size_t) k]);
                if (! was && field.isRunning())
                    lastTick = now;
            }
        }
    }

    if (field.isRunning())
    {
        /* Reduced motion turned on in the middle of a ring stops it there. */
        if (reduced())
            field.flatten();
        else
            field.advance ((now - lastTick) / 1000.0);
        lastTick = now;

        if (field.isRunning())
            drawChanged();
        else
            restored();
    }
    updateTimer();
}

void Ground::drawChanged()
{
    if (! canvas.isValid())
        return;

    juce::Rectangle<int> dirty;
    juce::Image::BitmapData out (canvas, juce::Image::BitmapData::readWrite);

    if (fresh)
    {
        /* The canvas starts as the static ground: the tile, from the origin. */
        const auto& tile = uv::ground::tile();
        const juce::Image::BitmapData in (tile, juce::Image::BitmapData::readOnly);
        jassert (in.pixelFormat == out.pixelFormat);
        for (int y = 0; y < out.height; ++y)
            for (int x = 0; x < out.width; x += uv::ground::tileSize)
                std::memcpy (out.getPixelPointer (x, y), in.getPixelPointer (0, y % uv::ground::tileSize),
                             (size_t) (juce::jmin (uv::ground::tileSize, out.width - x) * out.pixelStride));
        std::fill (drawn.begin(), drawn.end(), (std::uint8_t) ground::mid);
        fresh = false;
        dirty = getLocalBounds();
    }

    const juce::Image::BitmapData sprites (atlas(), juce::Image::BitmapData::readOnly);
    const auto canvasBounds = juce::Rectangle<int> (out.width, out.height);
    const int cols = field.dotsX(), rows = field.dotsY();

    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < cols; ++i)
        {
            const int level = field.dotLevel (i, j);
            auto& was = drawn[(size_t) (j * cols + i)];
            if (level == was)
                continue;
            was = (std::uint8_t) level;

            /* The sprite's square, centred on the dot, clipped to the canvas. */
            const juce::Rectangle<int> cell (i * pitch - half, j * pitch - half, pitch, pitch);
            const auto area = cell.getIntersection (canvasBounds);
            if (area.isEmpty())
                continue;
            const auto from = spriteAt (level, i, j) + (area.getPosition() - cell.getPosition());
            for (int y = 0; y < area.getHeight(); ++y)
                std::memcpy (out.getPixelPointer (area.getX(), area.getY() + y),
                             sprites.getPixelPointer (from.x, from.y + y),
                             (size_t) (area.getWidth() * out.pixelStride));
            dirty = dirty.isEmpty() ? area : dirty.getUnion (area);
        }

    if (! dirty.isEmpty())
        repaint (dirty);
}

void Ground::restored()
{
    fresh = true;
    repaint();
}

void Ground::resized()
{
    field.setSize (getWidth(), getHeight());
    if (getWidth() > 0 && getHeight() > 0)
        canvas = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), false, juce::SoftwareImageType());
    else
        canvas = {};
    drawn.assign ((size_t) (field.dotsX() * field.dotsY()), (std::uint8_t) ground::mid);
    refreshWalls();
    restored();
}

void Ground::paint (juce::Graphics& g)
{
    if (field.isRunning() && ! fresh && canvas.isValid())
        g.drawImageAt (canvas, 0, 0);
    else
        uv::ground::paint (g, getLocalBounds().toFloat());
}

} // namespace ni::ui
