// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The animated Ground as a component: the static ground at rest, pixel for
 * pixel; rings from the model played on its own clock; the Motion switch and
 * reduced motion; walls read from the window's boxes; and its timer running
 * only while there is something to do.
 */
#include "Ground.h"

#include "UvGround.h"
#include "UvTokens.h"
#include "WaveSource.h"
#include "pages.h"
#include "snapshot.h"

#include <doctest.h>

#include <deque>

using ni::ui::Ground;

namespace
{
constexpr double frameMs = 1000.0 / 30.0;

/* A ground on a clock the test moves, with a queue of rings for a model. */
struct Rig
{
    double ms = 0.0;
    bool reducedMotion = false;
    std::deque<float> queue;
    Ground ground { [this] { return ms; } };

    explicit Rig (int w = 240, int h = 144)
    {
        ground.setReducedMotionQuery ([this] { return reducedMotion; });
        ground.setBounds (0, 0, w, h);
    }

    void withModel()
    {
        ground.setRingSource ([this] (float* out, int capacity)
        {
            int n = 0;
            for (; n < capacity && ! queue.empty(); ++n)
            {
                out[n] = queue.front();
                queue.pop_front();
            }
            return n;
        });
    }

    void frames (int n)
    {
        for (int k = 0; k < n; ++k)
        {
            ms += frameMs;
            ground.tick();
        }
    }
};

juce::Image rest (int w, int h)
{
    juce::Image img (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    juce::Graphics g (img);
    uv::ground::paint (g, { 0.0f, 0.0f, (float) w, (float) h });
    return img;
}

int differing (const juce::Image& a, const juce::Image& b)
{
    return ni::ui::test::diff (a, b, 0).differing;
}
} // namespace

TEST_CASE ("ground: at rest it is the static ground, pixel for pixel")
{
    Rig rig (200, 120);
    CHECK (differing (ni::ui::test::render (rig.ground), rest (200, 120)) == 0);
    CHECK_FALSE (rig.ground.isMoving());
    CHECK_FALSE (rig.ground.isTicking());
}

TEST_CASE ("ground: a sprite at rest is the static tile's own square")
{
    const auto& tile = uv::ground::tile();
    for (const auto& dot : { juce::Point<int> { 1, 1 }, { 3, 2 }, { 7, 7 }, { 8, 9 } })
    {
        const auto s = ni::ui::ground::sprite (ni::ui::ground::mid, dot.x, dot.y);
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 12; ++x)
            {
                const int tx = (dot.x * 12 - 6 + x) % 96, ty = (dot.y * 12 - 6 + y) % 96;
                REQUIRE (s.getPixelAt (x, y) == tile.getPixelAt (tx, ty));
            }
    }

    /* On a peak the dot is bigger and brighter; in a valley, smaller and dimmer. */
    const auto top = ni::ui::ground::sprite (ni::ui::ground::levels - 1, 1, 1);
    const auto low = ni::ui::ground::sprite (0, 1, 1);
    const auto restS = ni::ui::ground::sprite (ni::ui::ground::mid, 1, 1);
    CHECK (top.getPixelAt (5, 5).getBrightness() > restS.getPixelAt (5, 5).getBrightness());
    CHECK (low.getPixelAt (5, 5).getBrightness() < restS.getPixelAt (5, 5).getBrightness());
    /* The bigger dot reaches a pixel the resting one does not. */
    CHECK (top.getPixelAt (4, 6).getBrightness() > restS.getPixelAt (4, 6).getBrightness());
}

TEST_CASE ("ground: a ring moves it on its own clock, and it comes back to rest")
{
    Rig rig;
    rig.ground.trigger (1.0f);
    CHECK (rig.ground.isMoving());
    CHECK (rig.ground.isTicking());

    rig.frames (20);
    CHECK (differing (ni::ui::test::render (rig.ground), rest (240, 144)) > 100);

    /* About 20 s to ring out; the picture is then the rest picture again. */
    rig.frames (30 * 40);
    CHECK_FALSE (rig.ground.isMoving());
    CHECK_FALSE (rig.ground.isTicking());
    CHECK (differing (ni::ui::test::render (rig.ground), rest (240, 144)) == 0);
}

TEST_CASE ("ground: a late frame steps by the time that passed, not by frames")
{
    Rig a, b;
    a.ground.trigger (1.0f);
    b.ground.trigger (1.0f);
    a.frames (6);
    /* The same 200 ms in two frames. */
    b.ms += 100.0;
    b.ground.tick();
    b.ms += 100.0;
    b.ground.tick();
    CHECK (a.ground.getField().time() == doctest::Approx (b.ground.getField().time()));
}

TEST_CASE ("ground: the model's rings are played, all of them, each frame")
{
    Rig rig;
    rig.withModel();
    CHECK (rig.ground.isTicking());
    rig.frames (3);
    CHECK_FALSE (rig.ground.isMoving());

    for (int k = 0; k < 20; ++k)
        rig.queue.push_back (0.1f);
    rig.frames (1);
    CHECK (rig.queue.empty());
    CHECK (rig.ground.isMoving());
    CHECK (rig.ground.getField().numRings() == 20);
}

TEST_CASE ("ground: Motion off flattens it and stops; rings from while it was off are dropped")
{
    Rig rig;
    rig.withModel();
    rig.queue.push_back (1.0f);
    rig.frames (10);
    REQUIRE (rig.ground.isMoving());

    rig.ground.setEnabled (false);
    CHECK_FALSE (rig.ground.isMoving());
    CHECK_FALSE (rig.ground.isTicking());
    CHECK (differing (ni::ui::test::render (rig.ground), rest (240, 144)) == 0);

    rig.ground.trigger (1.0f);
    CHECK_FALSE (rig.ground.isMoving());

    /* Counted by the plugin while off: never played late. */
    rig.queue.push_back (1.0f);
    rig.queue.push_back (0.55f);
    rig.ground.setEnabled (true);
    CHECK (rig.queue.empty());
    rig.frames (2);
    CHECK_FALSE (rig.ground.isMoving());
    CHECK (rig.ground.isTicking());
}

TEST_CASE ("ground: reduced motion turns it off regardless, at once")
{
    Rig rig;
    rig.withModel();
    rig.reducedMotion = true;
    rig.queue.push_back (1.0f);
    rig.frames (2);
    CHECK_FALSE (rig.ground.isMoving());
    rig.ground.trigger (1.0f);
    CHECK_FALSE (rig.ground.isMoving());

    rig.reducedMotion = false;
    rig.ground.trigger (1.0f);
    rig.frames (5);
    REQUIRE (rig.ground.isMoving());
    rig.reducedMotion = true;
    rig.frames (1);
    CHECK_FALSE (rig.ground.isMoving());
    CHECK (differing (ni::ui::test::render (rig.ground), rest (240, 144)) == 0);
}

TEST_CASE ("ground: the window's boxes are its walls, read again before a ring")
{
    juce::Component window;
    window.setSize (240, 144);
    Rig rig;
    window.addAndMakeVisible (rig.ground);
    juce::Component box;
    ni::ui::setWaveSource (box);
    window.addAndMakeVisible (box);
    box.setBounds (96, 48, 48, 48);

    rig.ground.setSourceRoot (&window);
    REQUIRE (rig.ground.getField().getWalls().size() == 1);
    CHECK (rig.ground.getField().getWalls()[0] == juce::Rectangle<float> (96.0f, 48.0f, 48.0f, 48.0f));

    /* The box moved; the next ring finds it where it is. */
    box.setBounds (24, 24, 48, 48);
    rig.ground.trigger (1.0f);
    CHECK (rig.ground.getField().getWalls()[0] == juce::Rectangle<float> (24.0f, 24.0f, 48.0f, 48.0f));

    /* The dots under the box never move. */
    rig.frames (30);
    const auto img = ni::ui::test::render (rig.ground);
    const auto still = rest (240, 144);
    for (int y = 36; y < 60; ++y)
        for (int x = 36; x < 60; ++x)
            REQUIRE (img.getPixelAt (x, y) == still.getPixelAt (x, y));
}

TEST_CASE ("ground: decoration -- no pointer, nothing for assistive technology")
{
    Ground g;
    bool self = true, children = true;
    g.getInterceptsMouseClicks (self, children);
    CHECK_FALSE (self);
    CHECK_FALSE (g.isAccessible());
    CHECK (g.isOpaque());
}

NI_SNAPSHOT_TEST ("ground: at rest, a downbeat, and a bar breaking around a panel")
{
    NI_CHECK_PAGE ("display-ground", "ground-moments");
}
