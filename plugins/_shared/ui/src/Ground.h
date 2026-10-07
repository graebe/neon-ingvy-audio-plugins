// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The animated window ground -- the Ground card of Ultraviolet 1.1.0, and the
 * web kit's Ground.jsx: the dot paper and its grain as the surface of a slow,
 * damped wave field that keeps the host's musical time.
 *
 * WHAT IT DOES. While the transport plays, every quarter note makes every box
 * edge and the window border emit a ring, each bar's downbeat a stronger one;
 * nothing rings while the transport is stopped. Rings travel through the
 * background only -- panels, wells and grids are solid to them -- reflect,
 * interfere and ring out over about 20 s. On a peak the dots grow and
 * brighten and the grain thickens; in a valley they shrink, dim and thin. The
 * grain itself never moves, only its density. GroundField.h is the model.
 *
 * WHERE THE RINGS COME FROM. The plugin counts them from the host's transport
 * on the audio thread (EditorModel::takeRings); the Ground polls that source
 * on its own timer at the design's frame rate (uv::tok::motion::ground::fps)
 * and plays them. trigger() rings one directly, for a test or the gallery.
 * Nothing here listens to the plugin's audio: the ground is the same in every
 * plugin, and independent of what it does to the sound.
 *
 * ITS CLOCK IS A TIMER, NEVER A DISPLAY FRAME, as the design requires: the
 * field steps by the time that really passed since the last tick, so a late
 * or missed tick leaves the picture late, never wrong, and a window the host
 * takes for hidden still moves. The timer runs while Motion is on and there
 * is a source to poll, or while the field moves; the field itself stops when
 * it has rung out.
 *
 * AT REST IT IS THE STATIC GROUND, PIXEL FOR PIXEL (uv::ground::paint): the
 * moving picture is drawn into a canvas the window's size, one sprite per dot
 * whose level changed since the last frame, and every sprite at rest is the
 * static tile's own pixels. Nothing is allocated per frame.
 *
 * OFF -- the Motion switch, or the system's reduced motion
 * (systemReducesMotion, which the Motion switch cannot override) -- it
 * flattens to rest at once and refuses rings, and rings that arrived while it
 * was off are dropped when it comes back on, never played late.
 *
 * Behind everything: the window's first child, the window's size, at its
 * origin, so its dots line up with the static ground's. It takes no pointer
 * and is invisible to assistive technology -- it carries no information the
 * user cannot hear. Message thread.
 */
#pragma once

#include "GroundField.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <vector>

namespace ni::ui
{

class Ground final : public juce::Component, private juce::Timer
{
public:
    /* Milliseconds, monotonic; injected by a test, the real one otherwise. */
    using Clock = std::function<double()>;
    /* Writes the rings since the last call, oldest first, at most `capacity`;
     * returns how many: EditorModel::takeRings. */
    using RingSource = std::function<int (float* strengths, int capacity)>;

    explicit Ground (Clock clock = {});
    ~Ground() override;

    void setRingSource (RingSource);

    /*
     * The boxes that emit and reflect, read from the components under `root`
     * marked as wave sources (WaveSource.h), in this component's coordinates:
     * read on a resize and again before each ring is played, so a layout that
     * moved has its walls where its boxes are by the next ring. nullptr for
     * none (only the window border emits). Not owned.
     */
    void setSourceRoot (juce::Component* root);
    /* Or the boxes directly, in this component's coordinates. */
    void setWalls (const std::vector<juce::Rectangle<float>>&);

    /* One ring of `strength` 0..1, now. */
    void trigger (float strength);

    /* The Motion switch. Off flattens at once. */
    void setEnabled (bool);
    bool isEnabled() const noexcept { return enabled; }

    /* Where the system's reduced-motion setting is read; systemReducesMotion
     * unless a test says otherwise. */
    void setReducedMotionQuery (std::function<bool()>);

    /* Whether the field moves. */
    bool isMoving() const noexcept { return field.isRunning(); }
    /* Whether the timer runs. */
    bool isTicking() const noexcept { return isTimerRunning(); }

    const ground::Field& getField() const noexcept { return field; }

    /* One frame: play the rings that arrived, step the field by the time that
     * passed, draw the dots that changed. The timer calls it; a test calls it
     * after moving its clock. */
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { tick(); }
    void updateTimer();
    void refreshWalls();
    bool reduced() const;
    void drawChanged();
    void restored();

    ground::Field field;
    Clock clock;
    RingSource rings;
    std::function<bool()> reducedQuery;
    juce::Component::SafePointer<juce::Component> sourceRoot;
    bool enabled = true;

    double lastTick = 0.0;

    /* The moving picture, the window's size, and the level each dot was last
     * drawn at in it. */
    juce::Image canvas;
    std::vector<std::uint8_t> drawn;
    bool fresh = true;

    std::array<float, 16> ringBuffer {};

    JUCE_DECLARE_NON_COPYABLE (Ground)
};

namespace ground
{
/* The sprite a dot is drawn with at `level`, for the dot (i, j) of the window:
 * a pitch square centred on it, exactly the static tile's pixels at `mid`.
 * For the tests. */
juce::Image sprite (int level, int i, int j);
} // namespace ground

} // namespace ni::ui
