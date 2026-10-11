// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Ground's wave field: the simulation, and nothing that draws -- a port
 * of the design system's own reference, design/scheme/project/components/
 * ground.js (UVGround.Field, 1.3.0).
 *
 * A PORT, NOT A DESIGN. Every number is the design's (uv::tok::motion::ground,
 * generated from ground.js), and the model is the brand book's (README,
 * Motion): the damped, viscous 2D wave equation
 *
 *   u_tt = c^2 lap(u) - gamma u_t + nu lap(u_t) + A s psi_f(t - t0) G(x - e)
 *
 * on a 6px grid, every second node a dot, lap the isotropic 9-point stencil
 * so a ring stays round. A ring is a drop: psi a Ricker wavelet peaking at f
 * (one slow wave of wavelength c / f), G a Gaussian of `spot` around one
 * emitter e, s its strength -- a circle that spreads, thins as it goes and
 * meets the boxes.
 *
 * THE EMITTERS stand just off the boxes' outer corners, `inset` along both
 * edges, where no other box and no border is within `clear`; with no box at
 * all, the window centre is one. The one nearest the centre is the anchor:
 * a downbeat (a ring of full strength) falls there and starts the bar's walk,
 * and every other ring falls at the next emitter clockwise around the centre.
 * Each ring's frequency and strength vary a little (varyF, varyS) by a hash
 * of its count since the grid was built -- the same every time.
 *
 * BOUNDARIES. Boxes reflect totally (Neumann: a neighbour inside a wall reads
 * as the node itself, so no phase flip), so rings echo between the boxes and
 * bend around their corners. The window border is an impedance edge -- Mur's
 * one-way update at the speed alpha c, alpha = (1 - R) / (1 + R) -- which
 * sends back borderR of a wave meeting it head on, so the window never fills
 * with its own standing waves. gamma = 2 / tau fades every wave alike; nu
 * fades it by its wavenumber squared, so ripple at the grid's scale dies at
 * once while the ring softens and lives on.
 *
 * DISPLAY. A dot shows tanh(u + slope (lambda / 2 pi) du/dl): the height plus
 * the slope towards a light at the top left, so a crest lights on the side
 * facing the light and shades on the other. At rest u is exactly zero and so
 * is every dot.
 *
 * STEPPED BY THE TIME THAT PASSED, at a fixed dt: advance() catches the
 * simulation up to the clock in steps of dt, at most maxSteps a frame, and
 * after a long stall drops the backlog rather than racing to catch up. When
 * no ring is sounding and every node is below `rest`, the field is set to
 * exactly zero and stops -- the ground at rest is the static design, and a
 * background that idles is a background that animates.
 *
 * NOTHING IS ALLOCATED PER STEP: the grids and the emitters' footprints are
 * made when the layout is set (setSize, setWalls), and the sounding rings
 * live in a fixed array. Message thread, like everything that draws from it.
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
    /* The boxes that reflect and carry the emitters, in the field's px.
     * Rebuilds the grid -- a ring mid-flight through a box that moved has
     * nowhere consistent to be -- but only if they changed: an unchanged
     * layout costs no ring. */
    void setWalls (const std::vector<juce::Rectangle<float>>&);
    const std::vector<juce::Rectangle<float>>& getWalls() const noexcept { return walls; }

    /*
     * One ring, `strength` 0..1 (1 on a downbeat, uv::tok::motion::beat::beat
     * on any other quarter note). A ring of full strength falls at the anchor
     * and starts the walk; any other at the next emitter. Ignored while
     * disabled, or with no emitter. Starts the field moving.
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

    /* The emitters in px, the anchor first, then clockwise around the window
     * centre: the order the beats of a bar walk them. */
    int numEmitters() const noexcept { return (int) emitters.size(); }
    juce::Point<float> emitter (int k) const { return emitters[(size_t) k].at; }
    int numRings() const noexcept { return ringCount; }

    /* The dots: one every pitch / cell nodes. */
    int dotsX() const noexcept { return dotCols; }
    int dotsY() const noexcept { return dotRows; }
    /* Dot (i, j)'s sprite level, 0..levels-1, lit and tanh soft-clipped;
     * `mid` for a dot inside a wall or off the grid. */
    int dotLevel (int i, int j) const noexcept;

    /* The most rings sounding at once: one lasts 2.5 / f s (1.7 s), and the
     * fastest metre rings a few a second. A ring past it replaces the oldest. */
    static constexpr int maxRings = 32;

private:
    void build();
    void placeEmitters();
    float lit (int gi, int gj) const noexcept;

    struct Emitter
    {
        juce::Point<float> at;
        /* The open nodes a drop here moves, and by how much. */
        std::vector<std::pair<int, float>> footprint;
    };

    struct Ring
    {
        double t0 = 0.0;
        float strength = 0.0f;
        float freq = 0.0f;
        int emitter = 0;
    };

    int width = 0, height = 0;
    int nx = 0, ny = 0, dotCols = 0, dotRows = 0;
    /* The present, the past, the next, and the scratch one step mixes. */
    std::vector<float> u, up, un, mix;
    std::vector<std::uint8_t> wall;
    std::vector<Emitter> emitters;
    std::vector<juce::Rectangle<float>> walls;

    std::array<Ring, maxRings> rings {};
    int ringCount = 0;
    /* Rings since the grid was built, and since the last downbeat. */
    std::uint32_t count = 0;
    int walk = 0;

    double t = 0.0;
    double acc = 0.0;
    bool enabled = true;
    bool running = false;
};

} // namespace ni::ui::ground
