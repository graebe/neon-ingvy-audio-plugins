// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The plot primitives: the well's frame and caption, the ruler's ladder and
 * its landmark, the band's extremes and gaps, and the colours each mark may
 * use -- the web kit's Plot.jsx and its tests, natively -- and the level range
 * an audio trace is drawn on: its rungs, its attack, its release, its floor,
 * its label and the well's full-scale view, on a clock the test turns.
 */
#include "Plot.h"

#include "Pointer.h"
#include "UvTokens.h"
#include "WaveSource.h"
#include "pages.h"
#include "snapshot.h"

#include <doctest.h>

#include <cmath>
#include <vector>

using namespace ni::ui;
namespace c = uv::tok::colour;

namespace
{
int subPaths (const juce::Path& p)
{
    int n = 0;
    for (juce::Path::Iterator it (p); it.next();)
        if (it.elementType == juce::Path::Iterator::startNewSubPath)
            ++n;
    return n;
}

std::vector<juce::String> labels (const std::vector<plot::Tick>& ticks)
{
    std::vector<juce::String> out;
    for (const auto& t : ticks)
        out.push_back (t.text);
    return out;
}

/* A well that draws one mark across its content. */
struct MarkWell final : public PlotWell
{
    std::function<void (juce::Graphics&, MarkWell&)> draw;
    void paintPlot (juce::Graphics& g) override
    {
        if (draw)
            draw (g, *this);
    }
};

/* A level, from decibels full scale. */
float level (float db)
{
    return std::pow (10.0f, db / 20.0f);
}

/* The test's clock: frames 60 a second, from wherever it stands. */
struct Frames
{
    double ms = 0.0;
    plot::LevelRange& range;

    /* `seconds` of frames, each with a peak at `db`; the largest move of the
     * range from one frame to the next. */
    float run (double seconds, float db)
    {
        float largest = 0.0f;
        for (const double end = ms + seconds * 1000.0; ms < end;)
        {
            ms += 1000.0 / 60.0;
            const float before = range.db();
            range.follow (level (db), ms);
            largest = std::max (largest, std::abs (range.db() - before));
        }
        return largest;
    }
};

/* A well with a level range, counting the rebuilds a range change asks for. */
struct RangeWell final : public PlotWell
{
    int rebuilds = 0;
    bool follow (float peak, double ms) { return followLevel (peak, ms); }
    void levelChanged() override { ++rebuilds; }
};

/* Whether anything brighter than the well is drawn in `box`. */
bool inked (const juce::Image& img, juce::Rectangle<int> box)
{
    for (int y = box.getY(); y < box.getBottom(); ++y)
        for (int x = box.getX(); x < box.getRight(); ++x)
            if (img.getPixelAt (x, y).getBrightness() > c::bg000.getBrightness() + 0.2f)
                return true;
    return false;
}
} // namespace

TEST_CASE ("plot: the ruler picks the smallest interval whose ticks stay 38px apart")
{
    /* 600px over 100 ms: 5 ms is 30px, 10 ms is 60px. */
    const auto ticks = plot::axisTicks (600.0f, 100.0);
    REQUIRE (ticks.size() == 11);
    CHECK (ticks.front().text == "0");
    CHECK (ticks[1].ms == 10.0);
    CHECK (ticks.back().text == "100");

    /* Past the ladder's end it stays on its last rung. */
    const auto long_ = plot::axisTicks (100.0f, 60000.0);
    REQUIRE (long_.size() >= 2);
    CHECK (long_[1].ms == 5000.0);
}

TEST_CASE ("plot: the landmark leads with its unit, and the ticks near it give way")
{
    /* 160 ms in 668px: a tick every 10 ms (41.75px); the 125 ms mark takes
     * the 120 and the 130 ms ticks, which are within 34px of it. */
    const auto ticks = plot::axisTicks (668.0f, 160.0, 125.0);
    REQUIRE_FALSE (ticks.empty());
    CHECK (ticks.front().text == "125 ms");
    const auto l = labels (ticks);
    CHECK (std::find (l.begin(), l.end(), "120") == l.end());
    CHECK (std::find (l.begin(), l.end(), "130") == l.end());
    CHECK (std::find (l.begin(), l.end(), "110") != l.end());
    CHECK (std::find (l.begin(), l.end(), "140") != l.end());

    /* A fractional landmark keeps its tenth; a whole one does not. */
    CHECK (plot::axisTicks (668.0f, 160.0, 62.5).front().text == "62.5 ms");
    CHECK (plot::axisTicks (668.0f, 160.0, 62.02).front().text == "62 ms");
    /* A landmark outside the span is no landmark. */
    CHECK (plot::axisTicks (668.0f, 160.0, 200.0).front().text == "0");
}

TEST_CASE ("plot: no ruler without a span or room for one")
{
    CHECK (plot::axisTicks (600.0f, 0.0).empty());
    CHECK (plot::axisTicks (600.0f, -5.0).empty());
    CHECK (plot::axisTicks (600.0f, std::numeric_limits<double>::infinity()).empty());
    CHECK (plot::axisTicks (6.0f, 100.0).empty());
}

TEST_CASE ("plot: the axis keeps its ticks and maps a time across the content")
{
    plot::Axis axis;
    axis.set (612.0f, 100.0);
    CHECK (axis.xOf (0.0) == doctest::Approx (6.0));
    CHECK (axis.xOf (100.0) == doctest::Approx (606.0));
    CHECK (axis.xOf (50.0) == doctest::Approx (306.0));
    const auto first = axis.ticks();
    axis.set (612.0f, 100.0);
    CHECK (axis.ticks() == first);
    axis.set (612.0f, 100.0, 40.0);
    CHECK (axis.ticks().front().text == "40 ms");
}

TEST_CASE ("plot: a band is the min and max behind each pixel, never a mean")
{
    /* 8 columns into 2 pixels: the spike in column 5 must survive. */
    std::vector<float> data (8 * 2, 0.0f);
    for (int i = 0; i < 8; ++i)
    {
        data[(size_t) i * 2] = -0.1f;
        data[(size_t) i * 2 + 1] = 0.1f;
    }
    data[5 * 2 + 1] = 1.0f;
    data[2 * 2] = -0.5f;

    juce::Path p;
    plot::band (p, { data.data(), 2, 8 }, 0, 1, { 10.0f, 2, 0.0f, 100.0f });
    REQUIRE (subPaths (p) == 1);
    const auto b = p.getBounds();
    /* hi 1.0 is the top; lo -0.5 is three quarters down. */
    CHECK (b.getY() == doctest::Approx (0.0f));
    CHECK (b.getBottom() == doctest::Approx (75.0f));
    CHECK (b.getX() == doctest::Approx (10.0f));
    CHECK (b.getRight() == doctest::Approx (11.0f));
}

TEST_CASE ("plot: values past full scale are held to the band")
{
    std::vector<float> data { -3.0f, 3.0f, -3.0f, 3.0f };
    juce::Path p;
    plot::band (p, { data.data(), 2, 2 }, 0, 1, { 0.0f, 2, 20.0f, 60.0f });
    CHECK (p.getBounds().getY() == doctest::Approx (20.0f));
    CHECK (p.getBounds().getBottom() == doctest::Approx (60.0f));
}

TEST_CASE ("plot: an unseen column breaks the band, and a lone pixel is no region")
{
    std::vector<float> data (10 * 2);
    for (int i = 0; i < 10; ++i)
    {
        data[(size_t) i * 2] = -0.5f;
        data[(size_t) i * 2 + 1] = 0.5f;
    }
    const plot::Capture cap { data.data(), 2, 10 };
    const plot::BandGeometry geom { 0.0f, 10, 0.0f, 10.0f };

    juce::Path p;
    plot::band (p, cap, 0, 1, geom, [] (int c) { return c != 4; });
    CHECK (subPaths (p) == 2);

    /* Columns 0..3 and then 5 alone: the lone one is dropped. */
    plot::band (p, cap, 0, 1, geom, [] (int c) { return c < 4 || c == 5; });
    CHECK (subPaths (p) == 1);
    CHECK (p.getBounds().getRight() == doctest::Approx (3.0f));

    /* Nothing seen, nothing drawn. */
    plot::band (p, cap, 0, 1, geom, [] (int) { return false; });
    CHECK (p.isEmpty());
}

TEST_CASE ("plot: no band from too little or the wrong shape of capture")
{
    std::vector<float> data { 0.0f, 1.0f, 0.0f, 1.0f };
    juce::Path p;
    p.addRectangle (0.0f, 0.0f, 5.0f, 5.0f);
    plot::band (p, { data.data(), 2, 1 }, 0, 1, { 0.0f, 10, 0.0f, 10.0f });
    CHECK (p.isEmpty());
    plot::band (p, { nullptr, 2, 2 }, 0, 1, { 0.0f, 10, 0.0f, 10.0f });
    CHECK (p.isEmpty());
    plot::band (p, { data.data(), 2, 2 }, 0, 2, { 0.0f, 10, 0.0f, 10.0f });
    CHECK (p.isEmpty());
    plot::band (p, { data.data(), 2, 2 }, 0, 1, { 0.0f, 0, 0.0f, 10.0f });
    CHECK (p.isEmpty());
}

TEST_CASE ("plot: the well's frame, content and caption")
{
    PlotWell well;
    well.setSize (300, 104);
    CHECK (well.contentBounds() == juce::Rectangle<float> (6.0f, 14.0f, 288.0f, 84.0f));
    CHECK (well.xAt (0.0f) == 6.0f);
    CHECK (well.xAt (1.0f) == 294.0f);
    CHECK (well.xAt (2.0f) == 294.0f);
    CHECK (well.xAt (-1.0f) == 6.0f);

    well.setCaption ("Signal   one cycle");
    CHECK (well.getCaption() == "SIGNAL   ONE CYCLE");
    CHECK (well.getTitle() == "SIGNAL   ONE CYCLE");

    CHECK (isWaveSource (well));
    CHECK_FALSE (well.getWantsKeyboardFocus());

    const auto img = test::render (well);
    CHECK (img.getPixelAt (0, 50) == c::line100);
    CHECK (img.getPixelAt (299, 50) == c::line100);
    CHECK (img.getPixelAt (150, 103) == c::line100);
    CHECK (img.getPixelAt (150, 50) == c::bg000);

    /* The caption, in ink-muted, in the band above the content. */
    bool captionInk = false;
    for (int x = 6; x < 60 && ! captionInk; ++x)
        for (int y = 2; y < 12 && ! captionInk; ++y)
            captionInk = img.getPixelAt (x, y).getBrightness() > c::bg000.getBrightness() + 0.2f;
    CHECK (captionInk);
}

TEST_CASE ("plot: each mark in the colour the card gives it")
{
    MarkWell well;
    well.setSize (200, 100);

    well.draw = [] (juce::Graphics& g, MarkWell&)
    {
        juce::Path area;
        area.addRectangle (20.0f, 20.0f, 40.0f, 40.0f);
        plot::under (g, area);
        juce::Path d;
        d.addRectangle (80.0f, 20.0f, 40.0f, 40.0f);
        plot::dry (g, d);
        plot::rule (g, 150.5f, 14.0f, 90.0f);
        plot::rule (g, 170.0f, 14.0f, 90.0f, true);
    };
    auto img = test::render (well);
    CHECK (img.getPixelAt (40, 40) == c::plotFill);
    CHECK (img.getPixelAt (100, 40) == c::bg000.interpolatedWith (c::plotDry, 0.5f));
    CHECK (img.getPixelAt (150, 50) == c::line200);
    CHECK (img.getPixelAt (169, 50) == c::amber);
    CHECK (img.getPixelAt (170, 50) == c::amber);

    well.draw = [] (juce::Graphics& g, MarkWell&)
    {
        juce::Path line;
        line.startNewSubPath (20.0f, 50.0f);
        line.lineTo (180.0f, 50.0f);
        plot::curve (g, line);
        juce::Path behind;
        behind.startNewSubPath (20.0f, 80.5f);
        behind.lineTo (180.0f, 80.5f);
        plot::ghost (g, behind);
    };
    img = test::render (well);
    /* The 2px line straddles y 50, uv; its glow lights the well beside it. */
    CHECK (img.getPixelAt (100, 49) == c::uv);
    CHECK (img.getPixelAt (100, 50) == c::uv);
    CHECK (img.getPixelAt (100, 53).getBlue() > c::bg000.getBlue() + 4);
    CHECK (img.getPixelAt (100, 80) == c::plotGhost);
}

TEST_CASE ("plot: the wet trace is uv over its glow")
{
    MarkWell well;
    well.setSize (200, 100);
    well.draw = [] (juce::Graphics& g, MarkWell&)
    {
        juce::Path area;
        area.addRectangle (50.0f, 40.0f, 100.0f, 20.0f);
        plot::wet (g, area);
    };
    const auto img = test::render (well);
    CHECK (img.getPixelAt (100, 50) == c::uv);
    CHECK (img.getPixelAt (100, 62).getBlue() > c::bg000.getBlue() + 4);
}

TEST_CASE ("plot: assistive technology sees a picture named by its caption")
{
    PlotWell well;
    well.setCaption ("Envelope");
    const auto handler = well.createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::image);
    CHECK (handler->getTitle() == "ENVELOPE");
}

TEST_CASE ("plot: a band's full scale is the level drawn at its edges")
{
    /* A -12 dBFS signal on a -12 dB range fills the band; on full scale, a
     * quarter of it either side of the middle. */
    std::vector<float> data { -0.25f, 0.25f, -0.25f, 0.25f };
    juce::Path p;
    plot::band (p, { data.data(), 2, 2 }, 0, 1, { 0.0f, 2, 0.0f, 100.0f, 0.25f });
    CHECK (p.getBounds().getY() == doctest::Approx (0.0f));
    CHECK (p.getBounds().getBottom() == doctest::Approx (100.0f));
    plot::band (p, { data.data(), 2, 2 }, 0, 1, { 0.0f, 2, 0.0f, 100.0f });
    CHECK (p.getBounds().getY() == doctest::Approx (37.5f));
    CHECK (p.getBounds().getBottom() == doctest::Approx (62.5f));
}

TEST_CASE ("plot: a capture's peak is the largest magnitude in the fields asked for, of the columns seen")
{
    std::vector<float> data { -0.1f, 0.2f, -0.9f,
                              -0.4f, 0.3f, 0.0f,
                              -0.1f, 0.6f, 0.0f };
    const plot::Capture cap { data.data(), 3, 3 };
    CHECK (plot::peak (cap, { 0, 1 }) == doctest::Approx (0.6f));
    CHECK (plot::peak (cap, { 0, 1, 2 }) == doctest::Approx (0.9f));
    CHECK (plot::peak (cap, { 0, 1 }, [] (int column) { return column < 2; }) == doctest::Approx (0.4f));
    /* A field the capture does not have is ignored; nothing at all is 0. */
    CHECK (plot::peak (cap, { 0, 7 }) == doctest::Approx (0.4f));
    CHECK (plot::peak ({ nullptr, 3, 3 }, { 0 }) == 0.0f);
    CHECK (plot::peak (cap, { 0, 1 }, [] (int) { return false; }) == 0.0f);
}

TEST_CASE ("plot: a peak fits under the next 6 dB rung, from full scale down to -48 dB")
{
    CHECK (plot::LevelRange::fit (1.0f) == 0.0f);
    CHECK (plot::LevelRange::fit (3.0f) == 0.0f);
    CHECK (plot::LevelRange::fit (level (-3.0f)) == 0.0f);
    CHECK (plot::LevelRange::fit (level (-7.0f)) == -6.0f);
    CHECK (plot::LevelRange::fit (level (-15.0f)) == -12.0f);
    /* On a rung is under it, a hair either side of the logarithm or not. */
    CHECK (plot::LevelRange::fit (level (-24.0f)) == -24.0f);
    CHECK (plot::LevelRange::fit (level (-23.995f)) == -24.0f);
    CHECK (plot::LevelRange::fit (level (-23.9f)) == -18.0f);
    /* The floor: quiet is not zoomed into, and silence is silence. */
    CHECK (plot::LevelRange::fit (level (-47.0f)) == -42.0f);
    CHECK (plot::LevelRange::fit (level (-49.0f)) == -48.0f);
    CHECK (plot::LevelRange::fit (level (-90.0f)) == -48.0f);
    CHECK (plot::LevelRange::fit (0.0f) == -48.0f);
    CHECK (plot::LevelRange::fit (std::numeric_limits<float>::quiet_NaN()) == -48.0f);
}

TEST_CASE ("plot: the level range lands on its first rung, and a louder peak widens it in the same frame")
{
    plot::LevelRange range;
    CHECK (range.db() == 0.0f);
    CHECK (range.follow (level (-20.0f), 1000.0));
    CHECK (range.db() == -18.0f);
    CHECK (range.fullScale() == doctest::Approx (level (-18.0f)));

    /* The attack: the frame the peak arrives in, whole rungs at a time. */
    CHECK (range.follow (level (-3.0f), 1016.0));
    CHECK (range.db() == 0.0f);
    CHECK_FALSE (range.follow (level (-3.0f), 1033.0));
}

TEST_CASE ("plot: the level range never clips the trace it follows")
{
    plot::LevelRange range;
    double ms = 0.0;
    /* A gate's pattern of loud and quiet with a swell under it. */
    for (int i = 0; i < 1200; ++i)
    {
        ms += 1000.0 / 60.0;
        const float db = -30.0f + 25.0f * (float) std::sin (i * 0.013) * (i % 40 < 20 ? 1.0f : 0.4f);
        range.follow (level (db), ms);
        CHECK (range.fullScale() >= level (db) * 0.999f);
    }
}

TEST_CASE ("plot: the level range waits out its window, then narrows smoothly to the rung that fits")
{
    plot::LevelRange range;
    Frames f { 0.0, range };
    f.run (1.0, -3.0f);
    REQUIRE (range.db() == 0.0f);

    /* Quiet now, but the loud second is inside the window: held. */
    f.run (1.7, -20.0f);
    CHECK (range.db() == 0.0f);

    /* Past it, a glide: no frame moves it far, and it takes a while. */
    const float largest = f.run (0.5, -20.0f);
    CHECK (range.db() < 0.0f);
    CHECK (range.db() > -18.0f);
    CHECK (largest < 1.5f);

    /* And it arrives, on the rung, inside two seconds. */
    f.run (1.5, -20.0f);
    CHECK (range.db() == -18.0f);
    CHECK (range.label() == juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "18 dB")));
}

TEST_CASE ("plot: material sitting on a rung does not flip the range between two")
{
    plot::LevelRange range;
    Frames f { 0.0, range };
    /* -10 dBFS wants -6; then -12.5, which fits -12, but within the margin of
     * it: the range stays where it is rather than pumping. */
    f.run (1.0, -10.0f);
    REQUIRE (range.db() == -6.0f);
    f.run (6.0, -12.5f);
    CHECK (range.db() == -6.0f);
    /* -14 has the margin, and narrows it. */
    f.run (6.0, -14.0f);
    CHECK (range.db() == -12.0f);
}

TEST_CASE ("plot: silence settles on the floor, and the floor is never passed")
{
    plot::LevelRange range;
    Frames f { 0.0, range };
    f.run (0.5, -6.5f);
    REQUIRE (range.db() == -6.0f);
    f.run (6.0, -200.0f);
    CHECK (range.db() == -48.0f);
    for (int i = 0; i < 60; ++i)
    {
        f.ms += 16.0;
        range.follow (0.0f, f.ms);
        CHECK (range.db() >= plot::LevelRange::floorDb);
    }
}

TEST_CASE ("plot: a clock that jumps or stands still neither stalls nor rushes the range")
{
    plot::LevelRange range;
    range.follow (level (-1.0f), 0.0);
    /* No time passes: nothing moves, whatever the peak. */
    range.follow (level (-33.0f), 0.0);
    CHECK (range.db() == 0.0f);
    /* An editor hidden for a minute: the window has long passed, and the
     * glide is a frame's step from a second, never a jump past its rung. */
    range.follow (level (-33.0f), 60000.0);
    CHECK (range.db() < 0.0f);
    CHECK (range.db() >= -30.0f);
    range.follow (level (-33.0f), 120000.0);
    CHECK (range.db() == -30.0f);
}

TEST_CASE ("plot: the level label is whole decibels with a minus sign, and says when it is fixed")
{
    plot::LevelRange range;
    CHECK (range.label() == "0 dB");
    range.follow (level (-15.0f), 0.0);
    CHECK (range.label() == juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "12 dB")));

    /* Fixed is full scale, and the range follows on underneath it. */
    range.setFixed (true);
    CHECK (range.isFixed());
    CHECK (range.db() == 0.0f);
    CHECK (range.fullScale() == 1.0f);
    CHECK (range.label() == "0 dB fixed");
    CHECK_FALSE (range.follow (level (-40.0f), 16.0));
    range.setFixed (false);
    CHECK (range.db() == -12.0f);
}

TEST_CASE ("plot: a well with a level range labels it over the trace, and a double-click holds full scale")
{
    RangeWell well;
    well.setSize (300, 104);
    well.setCaption ("Signal");
    const juce::Rectangle<int> labelBox { 200, 2, 94, 11 };

    /* Not asked for: no label, and a double-click is nothing. */
    CHECK_FALSE (well.hasLevelRange());
    CHECK_FALSE (inked (test::render (well), labelBox));
    gallery::Pointer p;
    p.doubleClick (well, { 150.0f, 50.0f });
    CHECK_FALSE (well.levelRange().isFixed());
    CHECK (well.rebuilds == 0);

    well.enableLevelRange();
    CHECK (well.hasLevelRange());
    CHECK (well.follow (level (-15.0f), 0.0));
    CHECK (well.levelRange().db() == -12.0f);
    /* Right-aligned to the content's edge, and not past it. */
    const auto img = test::render (well);
    CHECK (inked (img, { 280, 2, 14, 11 }));
    CHECK_FALSE (inked (img, { 294, 2, 5, 11 }));

    p.doubleClick (well, { 150.0f, 50.0f });
    CHECK (well.levelRange().isFixed());
    CHECK (well.rebuilds == 1);
    p.doubleClick (well, { 150.0f, 50.0f });
    CHECK_FALSE (well.levelRange().isFixed());
    CHECK (well.rebuilds == 2);

    /* The same, from outside: set to what it is already is no rebuild. */
    well.setLevelFixed (false);
    CHECK (well.rebuilds == 2);
}

TEST_CASE ("plot: the label keeps clear of a strip laid over the well's right edge")
{
    RangeWell well;
    well.setSize (300, 104);
    well.enableLevelRange (24.0f);
    well.follow (level (-15.0f), 0.0);
    const auto img = test::render (well);
    CHECK (inked (img, { 200, 2, 70, 11 }));
    CHECK_FALSE (inked (img, { 270, 2, 29, 11 }));
}

TEST_CASE ("plot: assistive technology presses a level range's well to hold it at full scale")
{
    RangeWell well;
    well.enableLevelRange();
    const auto handler = well.createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::image);
    REQUIRE (handler->getActions().invoke (juce::AccessibilityActionType::press));
    CHECK (well.levelRange().isFixed());

    /* A well without one takes no press. */
    PlotWell plain;
    const auto none = plain.createAccessibilityHandler();
    CHECK_FALSE (none->getActions().contains (juce::AccessibilityActionType::press));
}

NI_SNAPSHOT_TEST ("plot: envelope, signal, and a capture filling past its step")
{
    NI_CHECK_PAGE ("display-plot", "plot-wells");
}
