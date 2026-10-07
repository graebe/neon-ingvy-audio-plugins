// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Ground's wave field: its grid and sources, a ring's wave, how strength,
 * walls and time act on it, its coming to rest -- the design's ground.js and
 * the web kit's field tests, natively.
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

TEST_CASE ("ground field: the border and every box edge are sources; a box is a wall")
{
    auto f = sized (120, 96, { { 36.0f, 36.0f, 24.0f, 12.0f } });
    CHECK (f.isSource (0, 5));
    CHECK (f.isSource (20, 16));
    CHECK_FALSE (f.isSource (3, 3));
    /* Nodes 6..10 across, 6..8 down are inside the box, edges included. */
    CHECK (f.isWall (6, 6));
    CHECK (f.isWall (10, 8));
    CHECK_FALSE (f.isSource (8, 7));
    CHECK (f.isSource (5, 7));
    CHECK (f.isSource (8, 9));
    CHECK_FALSE (f.isWall (11, 7));
}

TEST_CASE ("ground field: a ring starts it; nothing moves without one")
{
    auto f = sized (240, 120);
    run (f, 1.0);
    CHECK_FALSE (f.isRunning());
    CHECK (f.peak() == 0.0f);

    f.trigger (1.0f);
    CHECK (f.isRunning());
    CHECK (f.numRings() == 1);
    run (f, 0.6);
    CHECK (f.peak() > 0.1f);
    /* "one downbeat peaks near u = 0.9" -- somewhere, at some moment. */
    float most = 0.0f;
    for (int k = 0; k < 40; ++k)
    {
        f.advance (frame);
        most = juce::jmax (most, f.peak());
    }
    CHECK (most > 0.4f);
    CHECK (most < 1.6f);
}

TEST_CASE ("ground field: strength scales the wave; rings add up")
{
    auto a = sized (240, 120);
    auto b = sized (240, 120);
    auto both = sized (240, 120);
    a.trigger (1.0f);
    b.trigger (uv::tok::motion::beat::beat);
    both.trigger (1.0f);
    both.trigger (uv::tok::motion::beat::beat);
    run (a, 0.8);
    run (b, 0.8);
    run (both, 0.8);

    /* The equation is linear: half the ring, half the wave; two rings, the
     * sum of their waves. */
    int checked = 0;
    for (int j = 0; j < a.nodesY(); j += 3)
        for (int i = 0; i < a.nodesX(); i += 3)
        {
            const float ua = a.displacement (i, j);
            if (std::abs (ua) < 0.05f)
                continue;
            ++checked;
            REQUIRE (b.displacement (i, j) / ua == doctest::Approx (uv::tok::motion::beat::beat).epsilon (0.01));
            REQUIRE (both.displacement (i, j) == doctest::Approx (ua + b.displacement (i, j)).epsilon (0.01));
        }
    CHECK (checked > 10);

    /* Strength is clamped to 0..1. */
    auto over = sized (240, 120);
    over.trigger (7.0f);
    run (over, 0.8);
    CHECK (over.displacement (6, 6) == doctest::Approx (a.displacement (6, 6)));
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

TEST_CASE ("ground field: it rings out to exactly nothing, and stops")
{
    auto f = sized (180, 120);
    f.trigger (1.0f);
    run (f, 2.0);
    CHECK (f.isRunning());
    CHECK (f.numRings() == 0);
    run (f, 40.0);
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

    /* A window that is all box has nowhere to ring. */
    auto shut = sized (60, 60, { { -1.0f, -1.0f, 62.0f, 62.0f } });
    CHECK (shut.numSources() == 0);
    shut.trigger (1.0f);
    CHECK_FALSE (shut.isRunning());
}

TEST_CASE ("ground field: the rings sounding at once are bounded")
{
    auto f = sized (120, 96);
    for (int k = 0; k < 100; ++k)
        f.trigger (0.5f);
    CHECK (f.numRings() == Field::maxRings);
}

TEST_CASE ("ground field: peaks draw above mid, valleys below, all within the sprites")
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
