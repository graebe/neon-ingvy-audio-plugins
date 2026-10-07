// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Ground's wave field. GroundField.h has the model and the rules.
 */
#include "GroundField.h"

#include "UvTokens.h"

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

    /* Sources: open nodes next to a wall, and on the border. */
    sources.clear();
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
            if (isSource (i, j))
                sources.push_back (j * nx + i);

    ringCount = 0;
    t = 0.0;
    acc = 0.0;
    running = false;
}

bool Field::isSource (int i, int j) const
{
    if (i < 0 || j < 0 || i >= nx || j >= ny)
        return false;
    const int q = j * nx + i;
    if (wall[(size_t) q])
        return false;
    const bool edge = i == 0 || j == 0 || i == nx - 1 || j == ny - 1;
    const bool near = (i > 0 && wall[(size_t) q - 1]) || (i < nx - 1 && wall[(size_t) q + 1])
                      || (j > 0 && wall[(size_t) (q - nx)]) || (j < ny - 1 && wall[(size_t) (q + nx)]);
    return near || (p::border && edge);
}

void Field::trigger (float strength)
{
    if (! enabled || sources.empty())
        return;
    if (ringCount == maxRings)
    {
        /* The oldest is the most nearly done: it gives way. */
        std::move (rings.begin() + 1, rings.end(), rings.begin());
        --ringCount;
    }
    rings[(size_t) ringCount++] = { t, juce::jlimit (0.0f, 1.0f, std::isfinite (strength) ? strength : 1.0f) };
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
    const double g = (2.0 / p::tau) * dt / 2.0;
    const auto a = (float) (1.0 / (1.0 + g));
    const auto b = (float) (1.0 - g);

    for (int j = 0; j < ny; ++j)
    {
        const int row = j * nx;
        for (int i = 0; i < nx; ++i)
        {
            const auto q = (size_t) (row + i);
            if (wall[q])
            {
                un[q] = 0.0f;
                continue;
            }
            const float c = u[q];
            const float l = i > 0 && ! wall[q - 1] ? u[q - 1] : c;
            const float r = i < nx - 1 && ! wall[q + 1] ? u[q + 1] : c;
            const float up_ = j > 0 && ! wall[q - (size_t) nx] ? u[q - (size_t) nx] : c;
            const float d = j < ny - 1 && ! wall[q + (size_t) nx] ? u[q + (size_t) nx] : c;
            un[q] = (2.0f * c - b * up[q] + k2 * (l + r + up_ + d - 4.0f * c)) * a;
        }
    }

    /* The sources: the sum of every ring still inside its wavelet. Summing
     * rather than replacing is what makes two rings interfere. */
    double force = 0.0;
    const double span = 2.5 / p::freq;
    int kept = 0;
    for (int i = 0; i < ringCount; ++i)
    {
        const double age = t - rings[(size_t) i].t0;
        if (age < span)
        {
            force += rings[(size_t) i].strength * ricker (age, p::freq);
            rings[(size_t) kept++] = rings[(size_t) i];
        }
    }
    ringCount = kept;

    if (force != 0.0)
    {
        const auto f = (float) (dt * dt * p::gain * force * a);
        for (const int q : sources)
            un[(size_t) q] += f;
    }

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

int Field::dotLevel (int i, int j) const noexcept
{
    const int gi = i * dotStep, gj = j * dotStep;
    if (i < 0 || j < 0 || gi >= nx || gj >= ny)
        return mid;
    const auto q = (size_t) (gj * nx + gi);
    if (wall[q])
        return mid;
    return juce::jlimit (0, levels - 1, (int) std::lround ((std::tanh (u[q]) + 1.0f) * (float) mid));
}

} // namespace ni::ui::ground
