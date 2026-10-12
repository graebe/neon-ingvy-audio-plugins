// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Ground's wave field. GroundField.h has the model and the rules.
 */
#include "GroundField.h"

#include "UvTokens.h"

#include <algorithm>
#include <cmath>

namespace ni::ui::ground
{

namespace p = uv::tok::motion::ground;

namespace
{
/* How many nodes apart two dots are: the pitch over the cell. */
constexpr int dotStep = (int) (p::pitch / p::cell + 0.5f);
static_assert (dotStep * (int) p::cell == (int) p::pitch, "the dots must sit on nodes");

/* A Ricker wavelet (the "Mexican hat"), peaking at frequency f. */
double ricker (double t, double f)
{
    double a = juce::MathConstants<double>::pi * f * (t - 1.0 / f);
    a *= a;
    return (1.0 - 2.0 * a) * std::exp (-a);
}

/* A ring's count -> two numbers in -1..1, the same every time (ground.js,
 * vary: a 32-bit integer mix). */
std::pair<float, float> vary (std::uint32_t n)
{
    std::uint32_t h = n;
    h = (h ^ (h >> 16)) * 0x45d9f3bu;
    h = (h ^ (h >> 16)) * 0x45d9f3bu;
    h ^= h >> 16;
    return { (float) ((double) (h & 0xffffu) / 32767.5 - 1.0), (float) ((double) (h >> 16) / 32767.5 - 1.0) };
}

/* How far (x, y) is from a box; 0 inside it. */
float toBox (juce::Point<float> at, const juce::Rectangle<float>& b)
{
    const float dx = juce::jmax (b.getX() - at.x, 0.0f, at.x - b.getRight());
    const float dy = juce::jmax (b.getY() - at.y, 0.0f, at.y - b.getBottom());
    return std::hypot (dx, dy);
}

/* h^2 lap(v) at node q, the isotropic 9-point stencil; a neighbour inside a
 * wall or off the grid reads as the node itself (Neumann). */
float laplacian (const float* v, const std::uint8_t* wall, int q, int i, int j, int nx, int ny) noexcept
{
    const float c = v[q];
    const bool l = i > 0, r = i < nx - 1, t = j > 0, b = j < ny - 1;
    const auto at = [&] (bool inside, int n) { return inside && ! wall[n] ? v[n] : c; };
    const float edges = at (l, q - 1) + at (r, q + 1) + at (t, q - nx) + at (b, q + nx);
    const float corners = at (l && t, q - nx - 1) + at (r && t, q - nx + 1) + at (l && b, q + nx - 1)
                          + at (r && b, q + nx + 1);
    return (4.0f * edges + corners - 20.0f * c) / 6.0f;
}
} // namespace

Field::Field() = default;

void Field::setSize (int w, int h)
{
    width = juce::jmax (0, w);
    height = juce::jmax (0, h);
    build();
}

void Field::setWalls (const std::vector<juce::Rectangle<float>>& next)
{
    if (next == walls)
        return;
    walls = next;
    build();
}

void Field::build()
{
    const float h = p::cell;
    nx = (int) std::floor ((float) width / h) + 1;
    ny = (int) std::floor ((float) height / h) + 1;
    dotCols = (int) std::floor ((float) width / p::pitch) + 1;
    dotRows = (int) std::floor ((float) height / p::pitch) + 1;
    const auto n = (size_t) nx * (size_t) ny;

    u.assign (n, 0.0f);
    up.assign (n, 0.0f);
    un.assign (n, 0.0f);
    mix.assign (n, 0.0f);
    wall.assign (n, 0);

    /* A node is wall when it lies inside a box, edges included. */
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
        {
            const float x = (float) i * h, y = (float) j * h;
            for (const auto& b : walls)
                if (x >= b.getX() && x <= b.getRight() && y >= b.getY() && y <= b.getBottom())
                {
                    wall[(size_t) (j * nx + i)] = 1;
                    break;
                }
        }

    placeEmitters();

    ringCount = 0;
    count = 0;
    walk = 0;
    t = 0.0;
    acc = 0.0;
    running = false;
}

void Field::placeEmitters()
{
    emitters.clear();

    /* A corner's emitter stands where open ground surrounds it: off every
     * other box by `clear`, inside the window by `clear`, and not on top of
     * an emitter already standing. */
    const float e = p::inset, room = p::clear;
    std::vector<juce::Point<float>> at;
    for (size_t r = 0; r < walls.size(); ++r)
    {
        const auto& b = walls[r];
        for (const auto corner : { juce::Point<float> (b.getX() - e, b.getY() - e),
                                   juce::Point<float> (b.getRight() + e, b.getY() - e),
                                   juce::Point<float> (b.getRight() + e, b.getBottom() + e),
                                   juce::Point<float> (b.getX() - e, b.getBottom() + e) })
        {
            bool ok = corner.x >= room && corner.y >= room && corner.x <= (float) width - room
                      && corner.y <= (float) height - room;
            for (size_t s = 0; ok && s < walls.size(); ++s)
                ok = s == r || toBox (corner, walls[s]) >= room;
            for (size_t s = 0; ok && s < at.size(); ++s)
                ok = corner.getDistanceFrom (at[s]) >= room;
            if (ok)
                at.push_back (corner);
        }
    }

    const juce::Point<float> centre ((float) width / 2.0f, (float) height / 2.0f);
    if (at.empty() && std::none_of (walls.begin(), walls.end(), [&] (const auto& b) { return toBox (centre, b) < room; }))
        at.push_back (centre);
    if (at.empty())
        return;

    /* The anchor is the emitter nearest the centre (ties in reading order);
     * the others follow it clockwise around the centre. */
    const auto first = [&] (juce::Point<float> a, juce::Point<float> b)
    {
        const float d = a.getDistanceFrom (centre) - b.getDistanceFrom (centre);
        if (std::abs (d) > 1.0e-6f)
            return d < 0.0f;
        return ! juce::exactlyEqual (a.y, b.y) ? a.y < b.y : a.x < b.x;
    };
    std::iter_swap (at.begin(), std::min_element (at.begin(), at.end(), first));
    const float from = std::atan2 (at[0].y - centre.y, at[0].x - centre.x);
    const auto turn = [&] (juce::Point<float> a)
    {
        const float d = std::atan2 (a.y - centre.y, a.x - centre.x) - from;
        return d < 0.0f ? d + juce::MathConstants<float>::twoPi : d;
    };
    std::stable_sort (at.begin() + 1, at.end(), [&] (auto a, auto b) { return turn (a) < turn (b); });

    /* Each emitter's footprint: the open nodes within 3 sigma, and their
     * Gaussian weights. */
    const float h = p::cell, s = p::spot;
    const int reach = (int) std::ceil (3.0f * s / h);
    for (const auto a : at)
    {
        Emitter em { a, {} };
        const int ci = juce::roundToInt (a.x / h), cj = juce::roundToInt (a.y / h);
        for (int j = juce::jmax (0, cj - reach); j <= juce::jmin (ny - 1, cj + reach); ++j)
            for (int i = juce::jmax (0, ci - reach); i <= juce::jmin (nx - 1, ci + reach); ++i)
            {
                const int q = j * nx + i;
                const float dx = (float) i * h - a.x, dy = (float) j * h - a.y, d2 = dx * dx + dy * dy;
                if (! wall[(size_t) q] && d2 <= 9.0f * s * s)
                    em.footprint.emplace_back (q, std::exp (-d2 / (2.0f * s * s)));
            }
        emitters.push_back (std::move (em));
    }
}

void Field::trigger (float strength)
{
    if (! enabled || emitters.empty())
        return;
    if (ringCount == maxRings)
    {
        /* The oldest is the most nearly done: it gives way. */
        std::move (rings.begin() + 1, rings.end(), rings.begin());
        --ringCount;
    }
    const float s = juce::jlimit (0.0f, 1.0f, std::isfinite (strength) ? strength : 1.0f);
    walk = s >= 1.0f ? 0 : walk + 1;
    const auto [vf, vs] = vary (count++);
    rings[(size_t) ringCount++] = { t, s * (1.0f + p::varyS * vs), p::freq * (1.0f + p::varyF * vf),
                                    walk % (int) emitters.size() };
    if (! running)
    {
        running = true;
        acc = 0.0;
    }
}

void Field::setEnabled (bool on)
{
    enabled = on;
    if (! on)
        flatten();
}

void Field::flatten()
{
    std::fill (u.begin(), u.end(), 0.0f);
    std::fill (up.begin(), up.end(), 0.0f);
    ringCount = 0;
    running = false;
    acc = 0.0;
}

void Field::step()
{
    const double dt = p::dt;
    const double k = p::speed * dt / p::cell;
    const auto k2 = (float) (k * k);
    const auto nu = (float) (p::visc * dt / (p::cell * p::cell));
    const double g = (2.0 / p::tau) * dt / 2.0;
    const auto a = (float) (1.0 / (1.0 + g));
    const auto b = (float) (1.0 - g);
    const auto* w = wall.data();

    /* k2 lap(u) + nu lap(u - up) is lap of one mix -- the stencil and its
     * mirroring are linear -- so one pass of it covers both terms. */
    for (size_t q = 0; q < u.size(); ++q)
        mix[q] = (k2 + nu) * u[q] - nu * up[q];

    for (int j = 0; j < ny; ++j)
    {
        const int row = j * nx;
        for (int i = 0; i < nx; ++i)
        {
            const int q = row + i;
            if (w[q])
            {
                un[(size_t) q] = 0.0f;
                continue;
            }
            un[(size_t) q] = (2.0f * u[(size_t) q] - b * up[(size_t) q] + laplacian (mix.data(), w, q, i, j, nx, ny)) * a;
        }
    }

    /* The border: an impedance edge, Mur's one-way update at the speed alpha
     * c, which sends back borderR of a wave meeting it head on. */
    if (p::borderR < 1.0f && nx > 1 && ny > 1)
    {
        const double ka = k * (1.0 - p::borderR) / (1.0 + p::borderR);
        const auto m = (float) ((ka - 1.0) / (ka + 1.0));
        const auto edge = [&] (int q, int in)
        {
            if (! w[q] && ! w[in])
                un[(size_t) q] = u[(size_t) in] + m * (un[(size_t) in] - u[(size_t) q]);
        };
        for (int j = 0; j < ny; ++j)
        {
            edge (j * nx, j * nx + 1);
            edge (j * nx + nx - 1, j * nx + nx - 2);
        }
        for (int i = 0; i < nx; ++i)
        {
            edge (i, i + nx);
            edge ((ny - 1) * nx + i, (ny - 2) * nx + i);
        }
    }

    /* The drops: every ring still inside its wavelet, at its own emitter.
     * Summing rather than replacing is what makes two rings interfere. */
    const double f0 = dt * dt * p::gain * a;
    int kept = 0;
    for (int r = 0; r < ringCount; ++r)
    {
        const auto& ring = rings[(size_t) r];
        const double age = t - ring.t0;
        if (age >= 2.5 / ring.freq)
            continue;
        rings[(size_t) kept++] = ring;
        const auto f = (float) (f0 * ring.strength * ricker (age, ring.freq));
        for (const auto& [q, weight] : emitters[(size_t) ring.emitter].footprint)
            un[(size_t) q] += f * weight;
    }
    ringCount = kept;

    /* Rotate the three buffers: the new present, the past, the scratch. */
    std::swap (up, u);
    std::swap (u, un);
    t += dt;
}

bool Field::advance (double seconds)
{
    if (! running)
        return false;

    acc += juce::jmin (0.1, juce::jmax (0.0, seconds));
    int n = 0;
    while (acc >= p::dt && n < (int) p::maxSteps)
    {
        step();
        acc -= p::dt;
        ++n;
    }
    /* After a long stall, drop the backlog instead of racing: the field would
     * show as suddenly running fast. */
    if (acc > p::dt)
        acc = 0.0;

    /* Rung out: back to EXACTLY zero, so every dot is back at rest. */
    if (ringCount == 0 && peak() <= p::rest)
        flatten();
    return running;
}

float Field::peak() const noexcept
{
    float m = 0.0f;
    for (const float v : u)
        m = juce::jmax (m, std::abs (v));
    return m;
}

float Field::lit (int gi, int gj) const noexcept
{
    const int q = gj * nx + gi;
    const float c = u[(size_t) q];
    const auto at = [&] (bool inside, int n) { return inside && ! wall[(size_t) n] ? u[(size_t) n] : c; };
    const float l = at (gi > 0, q - 1), r = at (gi < nx - 1, q + 1);
    const float up_ = at (gj > 0, q - nx), d = at (gj < ny - 1, q + nx);
    /* A surface rising towards the bottom right faces the light at the top
     * left: (dx + dy) / sqrt 2, times lambda / 2 pi so that a wave's slope
     * and its height are on the same scale. */
    const float towards = (r - l + d - up_) / (2.0f * p::cell) * juce::MathConstants<float>::sqrt2 * 0.5f;
    return c + p::slope * (p::speed / p::freq) / juce::MathConstants<float>::twoPi * towards;
}

int Field::dotLevel (int i, int j) const noexcept
{
    const int gi = i * dotStep, gj = j * dotStep;
    if (i < 0 || j < 0 || gi >= nx || gj >= ny)
        return mid;
    if (wall[(size_t) (gj * nx + gi)])
        return mid;
    return juce::jlimit (0, levels - 1, (int) std::lround ((std::tanh (lit (gi, gj)) + 1.0f) * (float) mid));
}

} // namespace ni::ui::ground
