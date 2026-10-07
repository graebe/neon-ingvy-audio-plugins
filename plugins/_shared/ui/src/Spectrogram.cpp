// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The rolling picture. Spectrogram.h has the design and the rules.
 */
#include "Spectrogram.h"

#include "ChildLights.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "WaveSource.h"

#include <array>
#include <cmath>
#include <cstring>

namespace ni::ui
{

namespace c = uv::tok::colour;

namespace
{
/* The ramp's stops, from the tokens: no colour is spelled here. */
const std::array<juce::Colour, 5>& stops()
{
    static const std::array<juce::Colour, 5> s { c::spec0, c::spec1, c::spec2, c::spec3, c::spec4 };
    return s;
}

/*
 * 256 levels over the five stops, interpolated in sRGB -- the stops were
 * chosen by eye against the picture they make, so interpolating elsewhere
 * would change what was judged. Rounded as the web kit's buildLut rounds.
 */
const std::array<juce::PixelARGB, 256>& lut()
{
    static const auto table = []
    {
        std::array<juce::PixelARGB, 256> t {};
        const auto& s = stops();
        constexpr int segments = 4;
        for (int v = 0; v < 256; ++v)
        {
            const double x = (double) v / 255.0 * segments;
            /* The last level sits exactly on the last stop. */
            const int i = juce::jmin (segments - 1, (int) std::floor (x));
            const double t01 = x - i;
            const auto mix = [&] (juce::uint8 a, juce::uint8 b)
            { return (juce::uint8) std::lround ((double) a + ((double) b - (double) a) * t01); };
            t[(size_t) v].setARGB (255, mix (s[(size_t) i].getRed(), s[(size_t) i + 1].getRed()),
                                   mix (s[(size_t) i].getGreen(), s[(size_t) i + 1].getGreen()),
                                   mix (s[(size_t) i].getBlue(), s[(size_t) i + 1].getBlue()));
        }
        return t;
    }();
    return table;
}

/* The clash's hatching (the layouts' SP4): ink lines 1.2px wide every 5px,
 * at 45 degrees, at 0.7 -- one period, tiled. */
constexpr float hatchPeriod = 5.0f;
constexpr float hatchWidth = 1.2f;
constexpr float hatchOpacity = 0.7f;

juce::Image makeHatch()
{
    const int n = (int) hatchPeriod;
    juce::Image tile (juce::Image::ARGB, n, n, true, juce::SoftwareImageType());
    for (int x = 0; x < n; ++x)
    {
        /* The line is centred on x = 0, wrapping: how much of pixel x it
         * covers. */
        const float left = (float) x, right = (float) x + 1.0f;
        const auto overlap = [&] (float a, float b) { return juce::jmax (0.0f, juce::jmin (right, b) - juce::jmax (left, a)); };
        const float half = hatchWidth * 0.5f;
        const float cover = overlap (-half, half) + overlap (hatchPeriod - half, hatchPeriod + half);
        for (int y = 0; y < n; ++y)
            tile.setPixelAt (x, y, c::ink.withAlpha (juce::jlimit (0.0f, 1.0f, cover)));
    }
    return tile;
}

/* Columns [srcX, srcX + width) of `src` into `dst` at dstX: a row of bytes
 * each, the same pixel format both sides. */
void copyColumns (const juce::Image& src, int srcX, juce::Image& dst, int dstX, int width)
{
    if (width <= 0)
        return;
    const juce::Image::BitmapData from (src, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData to (dst, juce::Image::BitmapData::writeOnly);
    jassert (from.pixelFormat == to.pixelFormat);
    for (int y = 0; y < from.height; ++y)
        std::memcpy (to.getPixelPointer (dstX, y), from.getPixelPointer (srcX, y),
                     (size_t) (width * from.pixelStride));
}
} // namespace

Spectrogram::Spectrogram (int columnCount, int bandCount)
    : columns (juce::jmax (1, columnCount)), hatch (makeHatch())
{
    setWaveSource (*this);
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setup (juce::jmax (1, bandCount));
    setSize (columns, bands);
}

Spectrogram::~Spectrogram() = default;

juce::Colour Spectrogram::ramp (std::uint8_t level)
{
    return juce::Colour (lut()[level].getUnpremultiplied());
}

void Spectrogram::setup (int bandCount)
{
    bands = bandCount;
    const auto image = [this] (juce::Image::PixelFormat format)
    { return juce::Image (format, columns, bands, true, juce::SoftwareImageType()); };

    history = image (juce::Image::ARGB);
    sweep = image (juce::Image::ARGB);
    frozen = image (juce::Image::ARGB);
    clashHistory = image (juce::Image::SingleChannel);
    clashSweep = image (juce::Image::SingleChannel);
    frozenClash = image (juce::Image::SingleChannel);

    const auto cells = (size_t) columns * (size_t) bands;
    levels.assign (cells, 0);
    sweepLevels.assign (cells, 0);
    frozenLevels.assign (cells, 0);
    headColumn.assign ((size_t) bands, 0);
    clashPrevious.assign ((size_t) bands, 0);
    hasFrozen = false;
    clear();
    /* Paused across a new axis: what is held is the empty picture of it. */
    if (paused)
        freeze();
}

void Spectrogram::clear()
{
    /* The floor colour, which is spec-0: silence is the well itself. */
    const auto floor = ramp (0);
    history.clear (history.getBounds(), floor);
    sweep.clear (sweep.getBounds(), floor);
    clashHistory.clear (clashHistory.getBounds());
    clashSweep.clear (clashSweep.getBounds());
    std::fill (levels.begin(), levels.end(), std::uint8_t (0));
    std::fill (sweepLevels.begin(), sweepLevels.end(), std::uint8_t (0));
    hasClashPrevious = false;
    cursor = 0;
    head = -1;
    playhead = -1;
    repaint();
    /* The crosshair points at a level measured against the old range. */
    if (pointer)
        report();
}

/* ------------------------------------------------------------- writing -- */

void Spectrogram::writeHistory (const std::uint8_t* column, int at)
{
    juce::Image::BitmapData out (history, at, 0, 1, bands, juce::Image::BitmapData::writeOnly);
    const auto& table = lut();
    /* Band 0 is the lowest and belongs at the bottom: row 0 is the top. */
    for (int b = 0; b < bands; ++b)
        reinterpret_cast<juce::PixelARGB*> (out.getPixelPointer (0, bands - 1 - b))->set (table[column[b]]);
    std::memcpy (levels.data() + (size_t) at * (size_t) bands, column, (size_t) bands);
}

void Spectrogram::writeSweep (const std::uint8_t* column, int slot)
{
    if (slot < 0 || slot >= columns)
        return;

    juce::Image::BitmapData out (sweep, juce::Image::BitmapData::writeOnly);
    const auto& table = lut();
    const auto put = [&] (int x, int b, std::uint8_t v)
    {
        reinterpret_cast<juce::PixelARGB*> (out.getPixelPointer (x, bands - 1 - b))->set (table[v]);
        sweepLevels[(size_t) x * (size_t) bands + (size_t) b] = v;
    };

    /* A column arrives every ~21 ms whatever the bar window is, so a wide
     * window gets fewer columns than slots: the slots between are filled
     * in. A jump most of the way round is a seek, not a gap. */
    int gap = head < 0 ? 0 : slot - head;
    if (gap < 0)
        gap += columns;
    if (gap > columns / 2)
        gap = 0;

    for (int g = 1; g < gap; ++g)
    {
        const double t = (double) g / (double) gap;
        const int at = (head + g) % columns;
        for (int b = 0; b < bands; ++b)
        {
            const double from = headColumn[(size_t) b];
            put (at, b, (std::uint8_t) std::lround (from + ((double) column[b] - from) * t));
        }
    }

    for (int b = 0; b < bands; ++b)
        put (slot, b, column[b]);
    std::memcpy (headColumn.data(), column, (size_t) bands);
    head = slot;
}

void Spectrogram::writeClash (const std::uint8_t* column, int historyAt, int slot)
{
    const auto put = [&] (juce::Image& layer, int x)
    {
        juce::Image::BitmapData out (layer, x, 0, 1, bands, juce::Image::BitmapData::writeOnly);
        for (int i = 0; i < bands; ++i)
        {
            auto* alpha = out.getPixelPointer (0, bands - 1 - i);
            const std::uint8_t v = column[i];
            if (v < clashEdge)
            {
                *alpha = 0;
                continue;
            }
            const std::uint8_t up = i + 1 < bands ? column[i + 1] : 0;
            const std::uint8_t down = i > 0 ? column[i - 1] : 0;
            const std::uint8_t back = hasClashPrevious ? clashPrevious[(size_t) i] : 0;
            const bool edge = up < clashEdge || down < clashEdge || back < clashEdge;
            *alpha = edge ? 255 : (juce::uint8) std::lround ((double) v / 255.0 * clashCeiling);
        }
    };

    put (clashHistory, historyAt);
    if (slot >= 0 && slot < columns)
        put (clashSweep, slot);
    std::memcpy (clashPrevious.data(), column, (size_t) bands);
    hasClashPrevious = true;
}

void Spectrogram::push (const Batch& batch)
{
    if (batch.data == nullptr || batch.count < 1 || batch.bands < 1)
        return;
    if (batch.bands != bands)
        setup (batch.bands);

    /* BOTH PICTURES, EVERY COLUMN, whichever one is on screen. */
    for (int i = 0; i < batch.count; ++i)
    {
        const auto* column = batch.data + (size_t) i * (size_t) bands;
        const int at = cursor;
        writeHistory (column, at);
        cursor = cursor + 1 == columns ? 0 : cursor + 1;

        const int slot = batch.slots != nullptr ? batch.slots[i] : -1;
        if (batch.slots != nullptr)
            writeSweep (column, slot);
        if (batch.clash != nullptr)
            writeClash (batch.clash + (size_t) i * (size_t) bands, at, slot);
    }

    if (paused)
        return;

    /* The playhead stops with the picture: it belongs to the frame shown. */
    if (batch.slots != nullptr)
        playhead = batch.slots[batch.count - 1];
    repaint();
    /* The pointer has not moved, but the picture under it has. */
    if (pointer)
        report();
}

/* ------------------------------------------------------- view and pause -- */

void Spectrogram::freeze()
{
    const bool bars = view == View::bars;
    const auto& srcLevels = bars ? sweepLevels : levels;
    const auto& picture = bars ? sweep : history;
    const auto& clashLayer = bars ? clashSweep : clashHistory;
    /* The sweep's slots are already x; the ring is rotated so that x is x. */
    const int origin = bars ? 0 : cursor;
    const int tail = columns - origin;

    copyColumns (picture, origin, frozen, 0, tail);
    copyColumns (picture, 0, frozen, tail, origin);
    copyColumns (clashLayer, origin, frozenClash, 0, tail);
    copyColumns (clashLayer, 0, frozenClash, tail, origin);

    const auto perColumn = (size_t) bands;
    std::memcpy (frozenLevels.data(), srcLevels.data() + (size_t) origin * perColumn, (size_t) tail * perColumn);
    std::memcpy (frozenLevels.data() + (size_t) tail * perColumn, srcLevels.data(), (size_t) origin * perColumn);
    hasFrozen = true;
}

void Spectrogram::setPaused (bool p)
{
    if (p == paused)
        return;
    paused = p;
    if (paused)
    {
        freeze();
        return;
    }
    hasFrozen = false;
    /* Catch the mark up to where the sweep got to while the picture was held. */
    if (head >= 0)
        playhead = head;
    repaint();
    if (pointer)
        report();
}

void Spectrogram::setView (View v)
{
    if (v == view)
        return;
    view = v;
    /* Paused, the snapshot is retaken from the view now chosen: a frozen
     * picture of the view just left would disagree with the switch. */
    if (paused)
        freeze();
    repaint();
    if (pointer)
        report();
}

void Spectrogram::setClash (bool shown)
{
    if (shown == clashShown)
        return;
    clashShown = shown;
    repaint();
}

void Spectrogram::setClashStyle (ClashStyle s)
{
    if (s == clashStyle)
        return;
    clashStyle = s;
    repaint();
}

/* ------------------------------------------------------------- reading -- */

std::uint8_t Spectrogram::levelAt (View v, int column, int band) const
{
    if (column < 0 || column >= columns || band < 0 || band >= bands)
        return 0;
    const auto& src = v == View::bars ? sweepLevels : levels;
    return src[(size_t) column * (size_t) bands + (size_t) band];
}

std::optional<Spectrogram::Sample> Spectrogram::sampleAt (juce::Point<float> p) const
{
    const float w = (float) getWidth(), h = (float) getHeight();
    if (bands < 1 || w <= 0.0f || h <= 0.0f || p.x < 0.0f || p.y < 0.0f || p.x >= w || p.y >= h)
        return std::nullopt;

    /* Paused: the snapshot's numbers, to match the snapshot's pixels. */
    const bool held = paused && hasFrozen;
    const bool bars = view == View::bars;
    const auto& src = held ? frozenLevels : (bars ? sweepLevels : levels);
    /* The cursor is the history's left edge; elsewhere a slot is an x. */
    const int origin = held || bars ? 0 : cursor;

    const int px = juce::jlimit (0, columns - 1, (int) std::floor (p.x / w * (float) columns));
    const int row = juce::jlimit (0, bands - 1, (int) std::floor (p.y / h * (float) bands));
    const int band = bands - 1 - row;
    const int slot = (origin + px) % columns;

    Sample s;
    s.x = p.x;
    s.y = p.y;
    s.band = band;
    s.age = columns - 1 - px;
    s.slot = px;
    s.level = src[(size_t) slot * (size_t) bands + (size_t) band];
    return s;
}

std::optional<Spectrogram::Sample> Spectrogram::hovered() const
{
    return pointer ? sampleAt (*pointer) : std::nullopt;
}

void Spectrogram::report()
{
    if (onHover)
        onHover (hovered());
}

/* ------------------------------------------------------------- drawing -- */

juce::Rectangle<float> Spectrogram::playheadBounds (int slot) const
{
    const float x = (float) slot / (float) columns * (float) getWidth();
    return { x, 0.0f, uv::tok::size::hairline, (float) getHeight() };
}

void Spectrogram::paint (juce::Graphics& g)
{
    const int w = getWidth(), h = getHeight();

    /* One layer, its source columns [srcX, srcX + srcW) to dest [x, x + dw). */
    const auto part = [&] (const juce::Image& img, int srcX, int srcW, int x, int dw, bool mask)
    {
        if (srcW > 0 && dw > 0)
            g.drawImage (img, x, 0, dw, h, srcX, 0, srcW, bands, mask);
    };

    /* A whole layer, rotated by `origin` columns: the ring's two copies. */
    const auto layer = [&] (const juce::Image& img, int origin, bool mask)
    {
        const int tail = columns - origin;
        const int split = juce::roundToInt ((double) tail / columns * w);
        part (img, origin, tail, 0, split, mask);
        part (img, 0, origin, split, w - split, mask);
    };

    const bool held = paused && hasFrozen;
    const bool bars = view == View::bars;
    const auto& picture = held ? frozen : (bars ? sweep : history);
    const auto& clash = held ? frozenClash : (bars ? clashSweep : clashHistory);
    const int origin = held || bars ? 0 : cursor;

    layer (picture, origin, false);

    if (clashShown)
    {
        juce::Graphics::ScopedSaveState state (g);
        if (clashStyle == ClashStyle::amber)
            g.setColour (c::amber);
        else
        {
            juce::FillType fill (hatch, juce::AffineTransform::rotation (juce::MathConstants<float>::pi * 0.25f));
            fill.setOpacity (hatchOpacity);
            g.setFillType (fill);
        }
        layer (clash, origin, true);
    }

    /* The sweep's playhead: where the music is. Brighter than the crosshair
     * and in uv rather than uv-deep -- the one mark here that reports rather
     * than responds. */
    if (bars && playhead >= 0)
    {
        g.setColour (c::uv.withAlpha (0.9f));
        g.fillRect (playheadBounds (playhead));
    }

    /* The crosshair: where the pointer is. No glow: it sits on the picture,
     * and a glow over a bright partial would read as part of the music. */
    if (pointer)
    {
        g.setColour (c::uvDeep.withAlpha (0.75f));
        g.fillRect (juce::Rectangle<float> (0.0f, pointer->y, (float) w, uv::tok::size::hairline));
        g.fillRect (juce::Rectangle<float> (pointer->x, 0.0f, uv::tok::size::hairline, (float) h));
    }
}

/* ------------------------------------------------------------- pointer -- */

void Spectrogram::repaintCrosshair()
{
    if (! pointer)
        return;
    repaint (0, (int) std::floor (pointer->y) - 1, getWidth(), 3);
    repaint ((int) std::floor (pointer->x) - 1, 0, 3, getHeight());
}

void Spectrogram::point (std::optional<juce::Point<float>> p)
{
    repaintCrosshair();
    if (p)
        p = juce::Point<float> (juce::jlimit (0.0f, (float) getWidth() - 0.001f, p->x),
                                juce::jlimit (0.0f, (float) getHeight() - 0.001f, p->y));
    pointer = p;
    repaintCrosshair();
    report();
}

void Spectrogram::mouseEnter (const juce::MouseEvent& e) { keyed = false; point (e.position); }
void Spectrogram::mouseMove (const juce::MouseEvent& e) { keyed = false; point (e.position); }
void Spectrogram::mouseDrag (const juce::MouseEvent& e) { keyed = false; point (e.position); }
void Spectrogram::mouseExit (const juce::MouseEvent&) { keyed = false; point (std::nullopt); }

void Spectrogram::mouseDown (const juce::MouseEvent& e)
{
    focus.pointerUsed();
    relight (*this);
    keyed = false;
    point (e.position);
}

/* ------------------------------------------------------------ keyboard -- */

juce::Point<float> Spectrogram::centre() const
{
    return { std::floor ((float) getWidth() * 0.5f) + 0.5f, std::floor ((float) getHeight() * 0.5f) + 0.5f };
}

bool Spectrogram::keyPressed (const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    const bool arrow = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
                    || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
    const bool end = code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey;

    /* Escape is the picture's only while there is a crosshair to take away. */
    if (code == juce::KeyPress::escapeKey && ! pointer)
        return false;
    if (! arrow && ! end && code != juce::KeyPress::escapeKey)
        return false;

    focus.keyUsed();
    relight (*this);

    if (code == juce::KeyPress::escapeKey)
    {
        keyed = false;
        point (std::nullopt);
        return true;
    }

    auto at = pointer.value_or (centre());
    const float step = key.getModifiers().isShiftDown() ? keyLeap : keyStep;
    if (code == juce::KeyPress::leftKey)        at.x -= step;
    else if (code == juce::KeyPress::rightKey)  at.x += step;
    else if (code == juce::KeyPress::upKey)     at.y -= step;
    else if (code == juce::KeyPress::downKey)   at.y += step;
    else if (code == juce::KeyPress::homeKey)   at.x = 0.5f;
    else                                        at.x = (float) getWidth() - 0.5f;

    keyed = true;
    point (at);   // clamped to the picture
    return true;
}

void Spectrogram::focusGained (FocusChangeType cause)
{
    focus.focusGained (cause);
    relight (*this);
    /* Tabbed onto: something to read at once, where the pointer is not. */
    if (focus.isVisible() && ! pointer)
    {
        keyed = true;
        point (centre());
    }
}

void Spectrogram::focusLost (FocusChangeType)
{
    focus.focusLost();
    relight (*this);
    if (keyed)
    {
        keyed = false;
        point (std::nullopt);
    }
}

void Spectrogram::paintLight (juce::Graphics& g)
{
    if (! isFocusVisible (*this))
        return;
    juce::Graphics::ScopedSaveState state (g);
    excludeOwnBounds (g, *this);
    uv::light::glowFocus (g, getLocalBounds().toFloat());
}

std::unique_ptr<juce::AccessibilityHandler> Spectrogram::createAccessibilityHandler()
{
    /* A picture: the editor names it and says what it shows (setInfo). */
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::image);
}

} // namespace ni::ui
