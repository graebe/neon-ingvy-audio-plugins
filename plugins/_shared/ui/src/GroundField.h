// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Ground's wave field: the simulation, and nothing that draws -- a port
 * of the design system's own reference, design/scheme/project/components/
 * ground.js (UVGround.Field, 1.1.0), as the web kit's lib/field.js ported it.
 *
 * A PORT, NOT A DESIGN. Every number is the design's (uv::tok::motion::ground,
 * generated from ground.js), and the model is the brand book's (README,
 * Motion): the damped 2D wave equation
 *
 *   u_tt = c^2 lap(u) - gamma u_t + A s psi(t - t0) S(x)
 *
 * on a 6px grid, every second node a dot. S marks the sources -- every open
 * node next to a box and every node on the window border -- psi is a Ricker
 * wavelet peaking at f0, so each ring is one slow wave of wavelength c / f0,
 * s its strength. Boxes and the border reflect (Neumann: a neighbour inside a
 * wall reads as the node itself, so no phase flip), rings superpose and so
 * interfere, and gamma = 2 / tau lets the field ring out over about 20 s.
 *
 * STEPPED BY THE TIME THAT PASSED, at a fixed dt: advance() catches the
 * simulation up to the clock in steps of dt, at most maxSteps a frame, and
 * after a long stall drops the backlog rather than racing to catch up. When
 * no ring is sounding and every node is below `rest`, the field is set to
 * exactly zero and stops -- the ground at rest is the static design, and a
 * background that idles is a background that animates.
 *
 * NOTHING IS ALLOCATED PER STEP: the grids are made when the layout is set
 * (setSize, setWalls), and the sounding rings live in a fixed array. Message
 * thread, like everything that draws from it.
 */
#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <cstdint>
#include <vector>

namespace ni::ui::ground
{

/* The sprite levels a node's displacement is quantised into for drawing;
 * `mid` is v = 0, the ground at rest. */
inline constexpr int levels = 25;
inline constexpr int mid = (levels - 1) / 2;

class Field
{
public:
    Field();

    /* The window, in px; rebuilds the grid, which resets the field. */
    void setSize (int width, int height);
    /* The boxes that emit and reflect, in the field's px. Rebuilds the grid
     * -- a ring mid-flight through a box that moved has nowhere consistent
     * to be -- but only if they changed: an unchanged layout costs no ring. */
    void setWalls (const std::vector<juce::Rectangle<float>>&);
    const std::vector<juce::Rectangle<float>>& getWalls() const noexcept { return walls; }

    /*
     * One ring, `strength` 0..1 (1 on a downbeat, uv::tok::motion::beat::beat
     * on any other quarter note). Ignored while disabled, or with no source
     * to emit it. Starts the field moving.
     */
    void trigger (float strength);

    /* The Motion switch, and the system's reduced motion folded in by the
     * caller: off flattens the field to rest at once and refuses rings. */
    void setEnabled (bool);
    bool isEnabled() const noexcept { return enabled; }

    /* Back to exactly zero, no ring sounding. */
    void flatten();

    /* Whether anything moves: a ring sounding, or a node above rest. */
    bool isRunning() const noexcept { return running; }

    /* Catches the simulation up by `seconds` (a frame's real time): whole
     * steps of dt, at most maxSteps. Comes to rest when it has rung out.
     * Returns isRunning(). */
    bool advance (double seconds);

    /* One step of dt, as advance() takes them. */
    void step();

    /* The largest |u| anywhere. */
    float peak() const noexcept;

    /* Simulated time since the grid was built, in s. */
    double time() const noexcept { return t; }

    /* The grid: nodes across and down, and a node's displacement. */
    int nodesX() const noexcept { return nx; }
    int nodesY() const noexcept { return ny; }
    float displacement (int i, int j) const noexcept { return u[(size_t) (j * nx + i)]; }
    bool isWall (int i, int j) const noexcept { return wall[(size_t) (j * nx + i)] != 0; }
    bool isSource (int i, int j) const;
    int numSources() const noexcept { return (int) sources.size(); }
    int numRings() const noexcept { return ringCount; }

    /* The dots: one every pitch / cell nodes. */
    int dotsX() const noexcept { return dotCols; }
    int dotsY() const noexcept { return dotRows; }
    /* Dot (i, j)'s sprite level, 0..levels-1, tanh soft-clipped; `mid` for
     * a dot inside a wall or off the grid. */
    int dotLevel (int i, int j) const noexcept;

    /* The most rings sounding at once: one lasts 2.5 / f0 s (1.7 s), and the
     * fastest metre rings a few a second. A ring past it replaces the oldest. */
    static constexpr int maxRings = 32;

private:
    void build();

    int width = 0, height = 0;
    int nx = 0, ny = 0, dotCols = 0, dotRows = 0;
    std::vector<float> u, up, un;
    std::vector<std::uint8_t> wall;
    std::vector<int> sources;
    std::vector<juce::Rectangle<float>> walls;

    struct Ring
    {
        double t0 = 0.0;
        float strength = 0.0f;
    };
    std::array<Ring, maxRings> rings {};
    int ringCount = 0;

    double t = 0.0;
    double acc = 0.0;
    bool enabled = true;
    bool running = false;
};

} // namespace ni::ui::ground
