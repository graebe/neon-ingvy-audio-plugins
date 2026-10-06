// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The plot primitives: the well's frame and caption, the ruler's ladder and
 * its landmark, the band's extremes and gaps, and the colours each mark may
 * use -- the web kit's Plot.jsx and its tests, natively.
 */
#include "Plot.h"

#include "UvTokens.h"
#include "WaveSource.h"
#include "pages.h"
#include "snapshot.h"

#include <doctest.h>

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

NI_SNAPSHOT_TEST ("plot: envelope, signal, and a capture filling past its step")
{
    NI_CHECK_PAGE ("display-plot", "plot-wells");
}
