// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Spectrogram's picture: the ramp, the ring and its rotation, the sweep
 * and its gap filler, the clash layer's edge and intensity, pause as a
 * repaint gate with its numbers frozen beside its pixels, and the crosshair's
 * sample -- the web kit's Spectrogram.jsx and its tests, natively. (Named so
 * as not to be taken for NI Spectrogram's editor tests, which are
 * spectrogram_*.cpp.)
 */
#include "Spectrogram.h"

#include "UvTokens.h"
#include "WaveSource.h"
#include "allocations.h"
#include "events.h"
#include "pages.h"
#include "snapshot.h"

#include <doctest.h>

#include <vector>

using ni::ui::Spectrogram;
namespace c = uv::tok::colour;

namespace
{
using Bytes = std::vector<std::uint8_t>;

/* One column of `bands` bytes, all `v`. */
Bytes flat (int bands, std::uint8_t v)
{
    return Bytes ((size_t) bands, v);
}

void pushOne (Spectrogram& s, const Bytes& column, int slot = -1, const Bytes* clash = nullptr)
{
    const int slots[1] { slot };
    s.push ({ column.data(), 1, (int) column.size(), slot >= 0 ? slots : nullptr,
              clash != nullptr ? clash->data() : nullptr });
}

/* How far apart two colours are, by their largest channel. */
int distance (juce::Colour a, juce::Colour b)
{
    return juce::jmax (std::abs ((int) a.getRed() - (int) b.getRed()),
                       std::abs ((int) a.getGreen() - (int) b.getGreen()),
                       std::abs ((int) a.getBlue() - (int) b.getBlue()));
}

float luminance (juce::Colour col)
{
    return 0.2126f * col.getRed() + 0.7152f * col.getGreen() + 0.0722f * col.getBlue();
}
} // namespace

TEST_CASE ("spectrogram: the ramp runs spec-0 to spec-4, silence the well itself")
{
    CHECK (Spectrogram::ramp (0) == c::spec0);
    CHECK (Spectrogram::ramp (255) == c::spec4);
    /* A quarter of the way is the second stop, within the rounding of 1/255. */
    CHECK (distance (Spectrogram::ramp (64), c::spec1) <= 2);
    CHECK (distance (Spectrogram::ramp (128), c::spec2) <= 2);
    CHECK (distance (Spectrogram::ramp (191), c::spec3) <= 2);
    /* Brighter is louder, all the way up. */
    for (int v = 1; v < 256; ++v)
        REQUIRE (luminance (Spectrogram::ramp ((std::uint8_t) v)) >= luminance (Spectrogram::ramp ((std::uint8_t) (v - 1))) - 0.5f);
}

TEST_CASE ("spectrogram: the shipped picture, solid to the rings, a crosshair to point with")
{
    Spectrogram s;
    CHECK (s.getWidth() == 606);
    CHECK (s.getHeight() == 256);
    CHECK (s.getColumns() == 606);
    CHECK (s.getBands() == 256);
    CHECK (s.isOpaque());
    CHECK (ni::ui::isWaveSource (s));
    CHECK_FALSE (s.getWantsKeyboardFocus());
    CHECK (s.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::CrosshairCursor));

    const auto handler = s.createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::image);
}

TEST_CASE ("spectrogram: columns go into the ring at the cursor, which wraps")
{
    Spectrogram s (4, 2);
    for (const std::uint8_t v : std::initializer_list<std::uint8_t> { 10, 20, 30 })
        pushOne (s, flat (2, v));
    CHECK (s.getCursor() == 3);
    CHECK (s.levelAt (Spectrogram::View::scroll, 0, 0) == 10);
    CHECK (s.levelAt (Spectrogram::View::scroll, 2, 1) == 30);

    pushOne (s, flat (2, 40));
    pushOne (s, flat (2, 50));
    CHECK (s.getCursor() == 1);
    CHECK (s.levelAt (Spectrogram::View::scroll, 0, 0) == 50);

    /* A batch of several is the same as one at a time. */
    Bytes three { 1, 1, 2, 2, 3, 3 };
    s.push ({ three.data(), 3, 2 });
    CHECK (s.getCursor() == 0);
    CHECK (s.levelAt (Spectrogram::View::scroll, 3, 0) == 3);

    /* Nothing, or nothing readable, writes nothing. */
    s.push ({ nullptr, 3, 2 });
    s.push ({ three.data(), 0, 2 });
    CHECK (s.getCursor() == 0);
}

TEST_CASE ("spectrogram: the newest column is at the right edge, band 0 at the bottom")
{
    Spectrogram s (4, 2);
    s.setSize (4, 2);
    for (const std::uint8_t v : std::initializer_list<std::uint8_t> { 10, 20, 30, 40, 50 })
    {
        Bytes col { v, 0 };
        pushOne (s, col);
    }
    /* Ring [50, 20, 30, 40], cursor 1: the picture reads 20, 30, 40, 50. */
    const auto img = ni::ui::test::render (s);
    CHECK (img.getPixelAt (0, 1) == Spectrogram::ramp (20));
    CHECK (img.getPixelAt (1, 1) == Spectrogram::ramp (30));
    CHECK (img.getPixelAt (2, 1) == Spectrogram::ramp (40));
    CHECK (img.getPixelAt (3, 1) == Spectrogram::ramp (50));
    /* Band 1, the top row, is silence. */
    CHECK (img.getPixelAt (3, 0) == c::spec0);
}

TEST_CASE ("spectrogram: the sweep puts a column at its slot and fills a gap, not a seek")
{
    Spectrogram s (16, 1);
    pushOne (s, flat (1, 0), 0);
    pushOne (s, flat (1, 100), 4);
    CHECK (s.levelAt (Spectrogram::View::bars, 1, 0) == 25);
    CHECK (s.levelAt (Spectrogram::View::bars, 2, 0) == 50);
    CHECK (s.levelAt (Spectrogram::View::bars, 3, 0) == 75);
    CHECK (s.levelAt (Spectrogram::View::bars, 4, 0) == 100);
    CHECK (s.getPlayhead() == 4);

    /* Round the end: 14 to 1 is a gap of 3 through the wrap. */
    pushOne (s, flat (1, 200), 14);
    pushOne (s, flat (1, 50), 1);
    CHECK (s.levelAt (Spectrogram::View::bars, 15, 0) == 150);
    CHECK (s.levelAt (Spectrogram::View::bars, 0, 0) == 100);

    /* More than half the way round is a seek: nothing between is touched. */
    pushOne (s, flat (1, 255), 10);
    for (int slot = 5; slot < 10; ++slot)
        CHECK (s.levelAt (Spectrogram::View::bars, slot, 0) == 0);
    CHECK (s.levelAt (Spectrogram::View::bars, 10, 0) == 255);

    /* The history took every one of them, in order. */
    CHECK (s.getCursor() == 5);
    CHECK (s.levelAt (Spectrogram::View::scroll, 4, 0) == 255);

    /* A slot off the picture is ignored, not wrapped. */
    pushOne (s, flat (1, 9), 99);
    CHECK (s.levelAt (Spectrogram::View::bars, 99 % 16, 0) != 9);
}

TEST_CASE ("spectrogram: switching views shows the other picture, complete")
{
    Spectrogram s (8, 1);
    s.setSize (8, 1);
    pushOne (s, flat (1, 200), 2);
    /* The next column moves the playhead off the one looked at. */
    pushOne (s, flat (1, 0), 3);
    s.setView (Spectrogram::View::bars);
    auto img = ni::ui::test::render (s);
    CHECK (img.getPixelAt (2, 0) == Spectrogram::ramp (200));
    CHECK (img.getPixelAt (1, 0) == c::spec0);
    s.setView (Spectrogram::View::scroll);
    img = ni::ui::test::render (s);
    CHECK (img.getPixelAt (6, 0) == Spectrogram::ramp (200));
    CHECK (img.getPixelAt (2, 0) == c::spec0);
}

TEST_CASE ("spectrogram: the clash is a layer over the picture, its edge drawn in full")
{
    /* Five bands, a region in the middle three: band 1 and 3 are its edge,
     * band 2 is inside it from the second column on. */
    Spectrogram s (3, 5);
    s.setSize (3, 5);
    const Bytes picture (5, 0);
    const Bytes clash { 0, 100, 255, 100, 0 };
    pushOne (s, picture, -1, &clash);
    pushOne (s, picture, -1, &clash);
    pushOne (s, picture, -1, &clash);

    auto img = ni::ui::test::render (s);
    CHECK (img.getPixelAt (1, 2) == c::spec0);

    s.setClash (true);
    img = ni::ui::test::render (s);
    /* Row 2 is band 2. Its first column has no column before it: an edge. */
    CHECK (distance (img.getPixelAt (0, 2), c::amber) <= 2);
    CHECK (distance (img.getPixelAt (1, 2), c::spec0.interpolatedWith (c::amber, 200.0f / 255.0f)) <= 3);
    /* Band 1 (row 3) has silence under it: the outline. */
    CHECK (distance (img.getPixelAt (1, 3), c::amber) <= 2);
    /* Below the edge level, nothing. */
    CHECK (img.getPixelAt (1, 0) == c::spec0);
    CHECK (img.getPixelAt (1, 4) == c::spec0);
}

TEST_CASE ("spectrogram: hatched, the clash is ink lines over the region and nothing else")
{
    Spectrogram s (40, 40);
    s.setSize (40, 40);
    Bytes clash (40, 0);
    for (int b = 10; b < 30; ++b)
        clash[(size_t) b] = 255;
    for (int i = 0; i < 40; ++i)
        pushOne (s, flat (40, 0), -1, &clash);
    s.setClash (true);
    s.setClashStyle (Spectrogram::ClashStyle::hatch);
    const auto img = ni::ui::test::render (s);

    int lit = 0, dark = 0;
    for (int y = 12; y < 28; ++y)
        for (int x = 2; x < 38; ++x)
            (luminance (img.getPixelAt (x, y)) > luminance (c::spec0) + 40.0f ? lit : dark) += 1;
    /* Lines a fifth of the period wide: some lit, most not. */
    CHECK (lit > 40);
    CHECK (dark > lit);
    /* Never amber: data, not a state. */
    CHECK (distance (img.getPixelAt (20, 20), c::amber) > 60);
    /* Outside the region, the picture. */
    for (int x = 0; x < 40; ++x)
        REQUIRE (img.getPixelAt (x, 2) == c::spec0);
}

TEST_CASE ("spectrogram: pause holds the picture and its numbers while columns go on")
{
    Spectrogram s (4, 1);
    s.setSize (4, 1);
    for (const std::uint8_t v : std::initializer_list<std::uint8_t> { 10, 20, 30, 40 })
        pushOne (s, flat (1, v), v / 10 - 1);

    s.setPaused (true);
    const auto held = ni::ui::test::render (s);
    const auto before = s.sampleAt ({ 3.5f, 0.5f });
    REQUIRE (before.has_value());
    CHECK (before->level == 40);

    pushOne (s, flat (1, 250), 0);
    pushOne (s, flat (1, 251), 1);
    /* Written behind the picture, not shown. */
    CHECK (s.levelAt (Spectrogram::View::scroll, 1, 0) == 251);
    CHECK (ni::ui::test::diff (ni::ui::test::render (s), held, 0).differing == 0);
    CHECK (s.sampleAt ({ 3.5f, 0.5f })->level == 40);
    CHECK (s.getPlayhead() == 3);

    /* Switching views while paused holds the other picture instead. */
    s.setView (Spectrogram::View::bars);
    CHECK (s.sampleAt ({ 0.5f, 0.5f })->level == 250);
    s.setView (Spectrogram::View::scroll);

    /* Resumed: the picture is current, the paused columns in it. */
    s.setPaused (false);
    CHECK (s.getPlayhead() == 1);
    const auto now = ni::ui::test::render (s);
    CHECK (now.getPixelAt (3, 0) == Spectrogram::ramp (251));
    CHECK (now.getPixelAt (2, 0) == Spectrogram::ramp (250));
    CHECK (s.sampleAt ({ 3.5f, 0.5f })->level == 251);
}

TEST_CASE ("spectrogram: clear empties both pictures and the playhead")
{
    Spectrogram s (4, 2);
    s.setSize (4, 2);
    pushOne (s, flat (2, 99), 2);
    s.clear();
    CHECK (s.getCursor() == 0);
    CHECK (s.getPlayhead() == -1);
    CHECK (s.levelAt (Spectrogram::View::scroll, 0, 0) == 0);
    CHECK (s.levelAt (Spectrogram::View::bars, 2, 0) == 0);
    const auto img = ni::ui::test::render (s);
    CHECK (img.getPixelAt (3, 0) == c::spec0);
}

TEST_CASE ("spectrogram: a new band count is a new axis, and the history is cleared")
{
    Spectrogram s (4, 2);
    pushOne (s, flat (2, 99));
    pushOne (s, flat (3, 77));
    CHECK (s.getBands() == 3);
    CHECK (s.getCursor() == 1);
    CHECK (s.levelAt (Spectrogram::View::scroll, 0, 2) == 77);
}

TEST_CASE ("spectrogram: writing columns allocates nothing")
{
    Spectrogram s (64, 32);
    s.setClash (true);
    const Bytes clash (32 * 3, 200);
    Bytes column (32 * 3);
    const int slots[3] { 0, 7, 9 };

    /* One batch first, for the statics a first call makes once (the ramp). */
    s.push ({ column.data(), 3, 32, slots, clash.data() });

    /* Every buffer exists before the first column: 1000 batches later, not
     * one allocation has been made on their account. */
    const auto before = ni::ui::test::allocations();
    for (int i = 0; i < 1000; ++i)
    {
        std::fill (column.begin(), column.end(), (std::uint8_t) (i % 256));
        s.push ({ column.data(), 3, 32, slots, clash.data() });
    }
    const auto after = ni::ui::test::allocations();
    CHECK (after - before == 0);
}

TEST_CASE ("spectrogram: the crosshair samples a band and a level, and says when it leaves")
{
    Spectrogram s (10, 4);
    s.setSize (100, 40);
    for (int i = 0; i < 10; ++i)
    {
        Bytes col { (std::uint8_t) (i * 10), 0, 0, 200 };
        pushOne (s, col);
    }

    std::vector<std::optional<Spectrogram::Sample>> heard;
    s.onHover = [&] (const std::optional<Spectrogram::Sample>& h) { heard.push_back (h); };

    /* x 95 is the newest column; y 35 the bottom row, band 0. */
    s.mouseMove (ni::ui::test::mouseAt (s, { 95.0f, 35.0f }));
    REQUIRE (heard.size() == 1);
    REQUIRE (heard.back().has_value());
    CHECK (heard.back()->band == 0);
    CHECK (heard.back()->level == 90);
    CHECK (heard.back()->age == 0);
    CHECK (heard.back()->slot == 9);

    /* The top row is the highest band. */
    s.mouseMove (ni::ui::test::mouseAt (s, { 5.0f, 2.0f }));
    CHECK (heard.back()->band == 3);
    CHECK (heard.back()->level == 200);
    CHECK (heard.back()->age == 9);

    /* A new column under a still pointer is reported. */
    const auto n = heard.size();
    pushOne (s, Bytes { 1, 2, 3, 4 });
    CHECK (heard.size() == n + 1);

    /* The two hairlines, uv-deep at 0.75, over the picture. */
    const auto img = ni::ui::test::render (s);
    const auto onLine = img.getPixelAt (50, 2);
    CHECK (distance (onLine, Spectrogram::ramp (200).interpolatedWith (c::uvDeep, 0.75f)) <= 4);

    s.mouseExit (ni::ui::test::mouseAt (s, { -5.0f, 2.0f }));
    CHECK_FALSE (heard.back().has_value());
    CHECK_FALSE (s.hovered().has_value());
    CHECK_FALSE (s.sampleAt ({ 100.0f, 0.0f }).has_value());
}

TEST_CASE ("spectrogram: the playhead is drawn in the bar view only")
{
    Spectrogram s (10, 2);
    s.setSize (10, 2);
    pushOne (s, flat (2, 0), 5);
    auto img = ni::ui::test::render (s);
    CHECK (img.getPixelAt (5, 0) == c::spec0);
    s.setView (Spectrogram::View::bars);
    img = ni::ui::test::render (s);
    CHECK (distance (img.getPixelAt (5, 0), c::spec0.interpolatedWith (c::uv, 0.9f)) <= 3);
    CHECK (img.getPixelAt (4, 0) == c::spec0);
}

NI_SNAPSHOT_TEST ("spectrogram: history, bars, and the clash as built and hatched")
{
    NI_CHECK_PAGE ("display-spectrogram", "spectrogram-states");
}
