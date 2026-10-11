// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Ground's wave field: its grid and emitters, a drop's round wave, how
 * strength, walls, the border and time act on it, its coming to rest -- the
 * design's ground.js (1.3.0), natively.
 */
#include "GroundField.h"

#include "UvTokens.h"
#include "allocations.h"

#include <doctest.h>

#include <cmath>

using ni::ui::ground::Field;
namespace p = uv::tok::motion::ground;

namespace
{
constexpr double frame = 1.0 / 30.0;

/* Runs the field for `seconds` in frames of the design's rate. */
void run (Field& f, double seconds)
{
    for (double t = 0.0; t < seconds - 1.0e-9; t += frame)
        f.advance (frame);
}

Field sized (int w, int h, std::vector<juce::Rectangle<float>> walls = {})
{
    Field f;
    f.setSize (w, h);
    f.setWalls (walls);
    return f;
}

/* The node nearest a point in px. */
float at (const Field& f, float x, float y)
{
    return f.displacement (juce::roundToInt (x / p::cell), juce::roundToInt (y / p::cell));
}

/* Where |u| is largest, in px. */
juce::Point<float> loudest (const Field& f)
{
    juce::Point<float> best;
    float most = -1.0f;
    for (int j = 0; j < f.nodesY(); ++j)
        for (int i = 0; i < f.nodesX(); ++i)
            if (std::abs (f.displacement (i, j)) > most)
            {
                most = std::abs (f.displacement (i, j));
                best = { (float) i * p::cell, (float) j * p::cell };
            }
    return best;
}

/* |u| at a point, step by step for `seconds`: when it peaked, and how high,
 * inside each window [from, to). */
struct Arrival
{
    double t = 0.0;
    float height = 0.0f;
};

Arrival loudestBetween (const std::vector<float>& trace, double from, double to)
{
    Arrival a;
    for (size_t s = 0; s < trace.size(); ++s)
    {
        const double t = (double) (s + 1) * p::dt;
        if (t >= from && t < to && std::abs (trace[s]) > a.height)
            a = { t, std::abs (trace[s]) };
    }
    return a;
}

std::vector<float> trace (Field& f, float x, float y, double seconds)
{
    std::vector<float> out;
    for (double t = 0.0; t < seconds; t += p::dt)
    {
        f.step();
        out.push_back (at (f, x, y));
    }
    return out;
}

/* The first ring a fresh field drops is ring 0: its frequency, varied. */
const double firstFreq = p::freq * (1.0 - p::varyF);
} // namespace

TEST_CASE ("ground field: a grid of 6px nodes, a dot on every second, at rest")
{
    auto f = sized (120, 96);
    CHECK (f.nodesX() == 21);
    CHECK (f.nodesY() == 17);
    CHECK (f.dotsX() == 11);
    CHECK (f.dotsY() == 9);
    CHECK_FALSE (f.isRunning());
    CHECK (f.peak() == 0.0f);
    for (int j = 0; j < f.dotsY(); ++j)
        for (int i = 0; i < f.dotsX(); ++i)
            REQUIRE (f.dotLevel (i, j) == ni::ui::ground::mid);
}

TEST_CASE ("ground field: emitters stand off the open corners, the anchor nearest the centre, then clockwise")
{
    auto f = sized (240, 180, { { 60.0f, 60.0f, 60.0f, 36.0f } });
    /* Nodes 10..20 across, 10..16 down are inside the box, edges included. */
    CHECK (f.isWall (10, 10));
    CHECK (f.isWall (20, 16));
    CHECK_FALSE (f.isWall (21, 13));

    REQUIRE (f.numEmitters() == 4);
    const float e = p::inset;
    /* The bottom right corner is nearest the centre (120, 90): the anchor.
     * Clockwise on screen from it: bottom left, top left, top right. */
    CHECK (f.emitter (0) == juce::Point<float> (120.0f + e, 96.0f + e));
    CHECK (f.emitter (1) == juce::Point<float> (60.0f - e, 96.0f + e));
    CHECK (f.emitter (2) == juce::Point<float> (60.0f - e, 60.0f - e));
    CHECK (f.emitter (3) == juce::Point<float> (120.0f + e, 60.0f - e));
}

TEST_CASE ("ground field: no emitter in a narrow gutter, near the border, or inside a box")
{
    /* Two boxes 10px apart: the corners facing the gutter are too close to
     * the other box; the outer ones are free. */
    auto gutter = sized (240, 150, { { 30.0f, 40.0f, 80.0f, 60.0f }, { 120.0f, 40.0f, 80.0f, 60.0f } });
    CHECK (gutter.numEmitters() == 4);
    for (int k = 0; k < gutter.numEmitters(); ++k)
    {
        const auto a = gutter.emitter (k);
        CHECK ((a.x < 30.0f || a.x > 200.0f));
    }

    /* A box against the border keeps only the corners that face open ground. */
    auto edge = sized (240, 150, { { 4.0f, 4.0f, 100.0f, 60.0f } });
    REQUIRE (edge.numEmitters() == 1);
    CHECK (edge.emitter (0) == juce::Point<float> (104.0f + p::inset, 64.0f + p::inset));

    /* No box: the window centre. A window that is all box: nowhere. */
    auto open = sized (200, 120);
    REQUIRE (open.numEmitters() == 1);
    CHECK (open.emitter (0) == juce::Point<float> (100.0f, 60.0f));
    auto shut = sized (60, 60, { { -1.0f, -1.0f, 62.0f, 62.0f } });
    CHECK (shut.numEmitters() == 0);
    shut.trigger (1.0f);
    CHECK_FALSE (shut.isRunning());
}

TEST_CASE ("ground field: a downbeat falls at the anchor, the bar's other beats walk the emitters")
{
    const std::vector<juce::Rectangle<float>> box { { 150.0f, 120.0f, 60.0f, 36.0f } };
    const float beat = uv::tok::motion::beat::beat;
    /* A downbeat, three beats, the next downbeat: anchor, 1, 2, 3, anchor. */
    const float bar[] { 1.0f, beat, beat, beat, 1.0f };
    auto where = sized (360, 276, box);
    REQUIRE (where.numEmitters() == 4);
    for (int k = 0; k < 5; ++k)
    {
        auto f = sized (360, 276, box);
        for (int r = 0; r < k; ++r)
            f.trigger (bar[r]);
        f.flatten();
        f.trigger (bar[k]);
        run (f, 0.5);
        /* Half a second on, the drop is still where it fell. */
        CHECK (loudest (f).getDistanceFrom (where.emitter (k % 4)) < 2.0f * p::cell);
    }
}

TEST_CASE ("ground field: a ring starts it; nothing moves without one")
{
    auto f = sized (400, 300);
    run (f, 1.0);
    CHECK_FALSE (f.isRunning());
    CHECK (f.peak() == 0.0f);

    f.trigger (1.0f);
    CHECK (f.isRunning());
    CHECK (f.numRings() == 1);

    /* "A downbeat lifts the dots ~300 px from its emitter" -- and, closer,
     * well past where tanh starts to clip. */
    float near = 0.0f, far = 0.0f;
    for (int k = 0; k < 90; ++k)
    {
        f.advance (frame);
        near = juce::jmax (near, std::abs (at (f, 260.0f, 150.0f)));
        far = juce::jmax (far, std::abs (at (f, 200.0f, 10.0f)) + std::abs (at (f, 390.0f, 150.0f)));
    }
    CHECK (near > 0.5f);
    CHECK (far > 0.1f);
}

TEST_CASE ("ground field: a ring is round")
{
    /* An open window, big enough that nothing comes back while it is timed:
     * the ring from the centre reaches a point on the axis and one on the
     * diagonal at the same speed and with the same height. */
    auto f = sized (480, 480);
    f.trigger (1.0f);
    const float cx = 240.0f, cy = 240.0f;
    std::vector<float> axis, diagonal;
    for (double t = 0.0; t < 2.4; t += p::dt)
    {
        f.step();
        axis.push_back (at (f, cx + 96.0f, cy));
        diagonal.push_back (at (f, cx + 66.0f, cy + 66.0f));   // 93.3 px out
    }
    const auto a = loudestBetween (axis, 0.0, 2.4);
    const auto d = loudestBetween (diagonal, 0.0, 2.4);
    CHECK (std::abs (a.t - d.t - (96.0 - 93.34) / p::speed) < 0.05);
    CHECK (d.height / a.height == doctest::Approx (1.0).epsilon (0.1));
}

TEST_CASE ("ground field: a box sends a ring back; the window border only some of it")
{
    /* A ring from the centre of a 360 x 360 window. On the right a box from
     * x = 300 on, 120px away: a probe 60px before it hears the ring, then its
     * echo. On the left the border, 180px away: a probe 60px before it hears
     * the ring, then the border's echo -- borderR of it, head on. Each echo is
     * measured against its own ring, so the spreading on the way cancels as
     * far as it can. */
    auto f = sized (360, 360, { { 300.0f, -10.0f, 200.0f, 380.0f } });
    REQUIRE (f.numEmitters() == 1);
    REQUIRE (f.emitter (0) == juce::Point<float> (180.0f, 180.0f));
    f.trigger (1.0f);
    std::vector<float> right, left;
    const double lag = 1.0 / firstFreq;
    for (double t = 0.0; t < 4.0; t += p::dt)
    {
        f.step();
        right.push_back (at (f, 240.0f, 180.0f));
        left.push_back (at (f, 60.0f, 180.0f));
    }
    /* Direct at 60 and 120px, echoes after 180 and 240px. */
    const auto rd = loudestBetween (right, 0.0, 1.2 + lag);
    const auto re = loudestBetween (right, 1.2 + lag, 2.4 + lag);
    const auto ld = loudestBetween (left, 0.0, 1.8 + lag);
    const auto le = loudestBetween (left, 1.8 + lag, 3.0 + lag);
    CHECK (rd.t == doctest::Approx (0.6 + lag).epsilon (0.15));
    CHECK (re.t == doctest::Approx (1.8 + lag).epsilon (0.15));
    CHECK (le.t == doctest::Approx (2.4 + lag).epsilon (0.15));

    /* A box is a mirror: the echo is all there, less the spreading
     * (sqrt (60 / 180) = 0.58) and the damping. */
    const float box = re.height / rd.height;
    CHECK (box > 0.4f);
    /* The border keeps half back, and spreading thins it less there
     * (sqrt (120 / 240) = 0.71) -- yet its echo is the weaker. */
    const float border = le.height / ld.height;
    CHECK (border > 0.1f);
    CHECK (border < 0.8f * box);
}

TEST_CASE ("ground field: between two boxes a ring echoes on and on")
{
    /* A 120px channel between two boxes, and the same window without them:
     * long after the ring has passed, the channel is still moving, the open
     * window, its border letting the ring go, is all but still. */
    const std::vector<juce::Rectangle<float>> channel { { -10.0f, -10.0f, 380.0f, 130.0f }, { -10.0f, 240.0f, 380.0f, 130.0f } };
    auto boxed = sized (360, 360, channel);
    auto open = sized (360, 360);
    REQUIRE (boxed.numEmitters() == 1);
    boxed.trigger (1.0f);
    open.trigger (1.0f);
    run (boxed, 3.0);
    run (open, 3.0);
    float inChannel = 0.0f, inOpen = 0.0f;
    for (int k = 0; k < 30; ++k)
    {
        boxed.advance (frame);
        open.advance (frame);
        for (float x = 30.0f; x <= 330.0f; x += 30.0f)
        {
            inChannel = juce::jmax (inChannel, std::abs (at (boxed, x, 180.0f)));
            inOpen = juce::jmax (inOpen, std::abs (at (open, x, 180.0f)));
        }
    }
    CHECK (inChannel > 3.0f * inOpen);
}

TEST_CASE ("ground field: no ripple at the grid's scale")
{
    /* A ring is a long wave: one node's curvature against its height is
     * about (k h)^2, 0.3 for 67px. Ripple at the grid's scale would be over
     * 1; the viscosity sees that it never builds up. */
    auto f = sized (360, 240, { { 60.0f, 60.0f, 90.0f, 60.0f }, { 210.0f, 120.0f, 90.0f, 60.0f } });
    for (int beat = 0; beat < 8; ++beat)
    {
        f.trigger (beat % 4 == 0 ? 1.0f : uv::tok::motion::beat::beat);
        run (f, 0.5);
    }
    float curve = 0.0f, height = 0.0f;
    for (int j = 1; j < f.nodesY() - 1; ++j)
        for (int i = 1; i < f.nodesX() - 1; ++i)
        {
            bool open = true;
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di)
                    open = open && ! f.isWall (i + di, j + dj);
            if (! open)
                continue;
            const float c = f.displacement (i, j);
            const float lap = f.displacement (i - 1, j) + f.displacement (i + 1, j) + f.displacement (i, j - 1)
                              + f.displacement (i, j + 1) - 4.0f * c;
            curve = juce::jmax (curve, std::abs (lap));
            height = juce::jmax (height, std::abs (c));
        }
    REQUIRE (height > 0.1f);
    CHECK (curve / height < 0.6f);
}

TEST_CASE ("ground field: strength scales the wave; rings add up")
{
    /* An open window: every ring falls at the centre. Each field drops the
     * same number of rings, so the k-th ring of each varies alike. */
    const float beat = uv::tok::motion::beat::beat;
    auto a = sized (240, 120);
    auto b = sized (240, 120);
    auto both = sized (240, 120);
    a.trigger (1.0f);
    a.trigger (0.0f);
    b.trigger (0.0f);
    b.trigger (beat);
    both.trigger (1.0f);
    both.trigger (beat);
    auto half = sized (240, 120);
    half.trigger (0.5f);
    auto one = sized (240, 120);
    one.trigger (1.0f);
    run (a, 0.8);
    run (b, 0.8);
    run (both, 0.8);
    run (half, 0.8);
    run (one, 0.8);

    /* The equation is linear: half the ring, half the wave; two rings, the
     * sum of their waves. */
    int checked = 0;
    for (int j = 0; j < a.nodesY(); j += 3)
        for (int i = 0; i < a.nodesX(); i += 3)
        {
            const float ua = one.displacement (i, j);
            if (std::abs (ua) < 0.05f)
                continue;
            ++checked;
            REQUIRE (half.displacement (i, j) / ua == doctest::Approx (0.5).epsilon (0.01));
            REQUIRE (both.displacement (i, j)
                     == doctest::Approx (a.displacement (i, j) + b.displacement (i, j)).epsilon (0.01));
        }
    CHECK (checked > 10);

    /* Strength is clamped to 0..1. */
    auto over = sized (240, 120);
    over.trigger (7.0f);
    run (over, 0.8);
    CHECK (over.displacement (6, 6) == doctest::Approx (one.displacement (6, 6)));
}

TEST_CASE ("ground field: the same rings make the same field")
{
    const std::vector<juce::Rectangle<float>> boxes { { 40.0f, 40.0f, 100.0f, 60.0f }, { 200.0f, 90.0f, 80.0f, 60.0f } };
    auto a = sized (320, 200, boxes);
    auto b = sized (320, 200, boxes);
    for (int k = 0; k < 6; ++k)
    {
        a.trigger (k % 4 == 0 ? 1.0f : 0.55f);
        b.trigger (k % 4 == 0 ? 1.0f : 0.55f);
        run (a, 0.5);
        run (b, 0.5);
    }
    for (int j = 0; j < a.nodesY(); ++j)
        for (int i = 0; i < a.nodesX(); ++i)
            REQUIRE (a.displacement (i, j) == b.displacement (i, j));
}

TEST_CASE ("ground field: walls stay still, and the wave breaks around them")
{
    auto f = sized (240, 144, { { 96.0f, 48.0f, 48.0f, 48.0f } });
    f.trigger (1.0f);
    for (int k = 0; k < 60; ++k)
    {
        f.advance (frame);
        for (int j = 8; j <= 16; ++j)
            for (int i = 16; i <= 24; ++i)
                REQUIRE (f.displacement (i, j) == 0.0f);
    }
    /* Inside a wall a dot is drawn at rest. */
    CHECK (f.dotLevel (10, 6) == ni::ui::ground::mid);
}

TEST_CASE ("ground field: it fades, rings out to exactly nothing, and stops")
{
    auto f = sized (360, 240, { { 60.0f, 60.0f, 90.0f, 60.0f } });
    f.trigger (1.0f);
    run (f, 2.0);
    CHECK (f.isRunning());
    CHECK (f.numRings() == 0);

    /* The energy, a second at a time, only ever falls once the ring is out. */
    double last = 1.0e30;
    for (int second = 0; second < 8; ++second)
    {
        double energy = 0.0;
        for (int k = 0; k < 30; ++k)
        {
            f.advance (frame);
            for (int j = 0; j < f.nodesY(); ++j)
                for (int i = 0; i < f.nodesX(); ++i)
                    energy += (double) f.displacement (i, j) * f.displacement (i, j);
        }
        CHECK (energy < last);
        last = energy;
    }

    run (f, 60.0);
    CHECK_FALSE (f.isRunning());
    CHECK (f.peak() == 0.0f);
    CHECK_FALSE (f.advance (frame));
}

TEST_CASE ("ground field: a stall is caught up by at most maxSteps, then dropped")
{
    auto f = sized (120, 96);
    f.trigger (1.0f);
    f.advance (5.0);
    /* A 0.1 s cap on the catch-up, in steps of dt, at most maxSteps of them:
     * the field is 0.1 s on, not 5. */
    CHECK (f.time() <= p::maxSteps * p::dt + 1.0e-9);
    CHECK (f.time() >= (p::maxSteps - 1.0) * p::dt - 1.0e-9);
    /* The backlog is gone: the next frame is one frame's worth of steps. */
    const double before = f.time();
    f.advance (frame);
    CHECK (f.time() - before <= frame + p::dt + 1.0e-9);
    CHECK (f.time() - before >= frame - p::dt - 1.0e-9);
}

TEST_CASE ("ground field: off flattens it and refuses rings; a box that moves resets it")
{
    auto f = sized (180, 120);
    f.trigger (1.0f);
    run (f, 0.5);
    f.setEnabled (false);
    CHECK_FALSE (f.isRunning());
    CHECK (f.peak() == 0.0f);
    f.trigger (1.0f);
    CHECK_FALSE (f.isRunning());
    f.setEnabled (true);

    f.trigger (1.0f);
    run (f, 0.5);
    /* The same boxes again: nothing happens to the ring in flight. */
    f.setWalls ({});
    CHECK (f.isRunning());
    f.setWalls ({ { 60.0f, 30.0f, 30.0f, 30.0f } });
    CHECK_FALSE (f.isRunning());
    CHECK (f.peak() == 0.0f);
}

TEST_CASE ("ground field: the rings sounding at once are bounded")
{
    auto f = sized (120, 96);
    for (int k = 0; k < 100; ++k)
        f.trigger (0.5f);
    CHECK (f.numRings() == Field::maxRings);
}

TEST_CASE ("ground field: lit from the top left, a ring draws above mid and below, all within the sprites")
{
    auto f = sized (240, 120);
    f.trigger (1.0f);
    bool above = false, below = false;
    for (int k = 0; k < 60; ++k)
    {
        f.advance (frame);
        for (int j = 0; j < f.dotsY(); ++j)
            for (int i = 0; i < f.dotsX(); ++i)
            {
                const int l = f.dotLevel (i, j);
                REQUIRE (l >= 0);
                REQUIRE (l < ni::ui::ground::levels);
                above = above || l > ni::ui::ground::mid;
                below = below || l < ni::ui::ground::mid;
            }
    }
    CHECK (above);
    CHECK (below);
    CHECK (f.dotLevel (-1, 0) == ni::ui::ground::mid);
    CHECK (f.dotLevel (0, 99) == ni::ui::ground::mid);

    /* The light, from the top left: across the light's axis a ring is drawn
     * the same on both sides; along it, one side of each crest is lit and
     * the other shaded, so the two sides differ. */
    auto lit = sized (480, 480);
    lit.trigger (1.0f);
    run (lit, 1.5);
    int differs = 0;
    for (int d = 1; d < 20; ++d)
    {
        REQUIRE (lit.dotLevel (20 + d, 20 - d) == lit.dotLevel (20 - d, 20 + d));
        differs += std::abs (lit.dotLevel (20 - d, 20 - d) - lit.dotLevel (20 + d, 20 + d));
    }
    CHECK (differs > 4);
}

TEST_CASE ("ground field: stepping allocates nothing")
{
    auto f = sized (720, 470, { { 32.0f, 76.0f, 656.0f, 258.0f } });
    f.trigger (1.0f);
    f.advance (frame);
    const auto before = ni::ui::test::allocations();
    for (int k = 0; k < 120; ++k)
    {
        if (k % 15 == 0)
            f.trigger (0.55f);
        f.advance (frame);
    }
    const auto after = ni::ui::test::allocations();
    CHECK (after - before == 0);
}
