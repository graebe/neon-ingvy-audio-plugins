// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Ground as its card describes it, held still at three moments: at rest,
 * where it is the static ground; one downbeat 0.6 s on, a ring leaving the
 * border and the panel's edges; and a 4/4 bar at 120 BPM 2.5 s on, the rings
 * crossing and interfering, breaking around the panel and never through it.
 *
 * Each ground runs on a clock of its own that this page moves by hand, frame
 * by frame at the design's rate, so the pictures are the same every time.
 */
#include "DisplayPage.h"
#include "Gallery.h"

#include "Ground.h"
#include "Panel.h"

#include <array>

namespace
{

using namespace ni::ui;

constexpr int wide = 680, tall = 120;

struct Moment
{
    double ms = 0.0;
    Ground ground { [this] { return ms; } };
    Panel panel { "Gate" };

    /* Plays `rings` (strength, at ms) and runs the frames up to `until`. */
    void play (std::initializer_list<std::pair<float, double>> rings, double until)
    {
        const double frame = 1000.0 / (double) uv::tok::motion::ground::fps;
        auto next = rings.begin();
        for (ms = 0.0; ms <= until; ms += frame)
        {
            while (next != rings.end() && next->second <= ms)
                ground.trigger ((next++)->first);
            ground.tick();
        }
    }
};

struct GroundPage final : public gallery::DisplayPage
{
    static constexpr int row = 22 + tall + 16;

    GroundPage()
    {
        setSize (wide, 3 * row - 16);
        const char* names[] { "at rest", "a downbeat, 0.6 s on", "a bar of 4/4 at 120 bpm, 2.5 s on" };
        for (int i = 0; i < 3; ++i)
        {
            auto& m = moments[(size_t) i];
            const int y = i * row;
            caption (names[i], { 0.0f, (float) y, (float) wide, 14.0f });
            addAndMakeVisible (m.ground);
            m.ground.setBounds (0, y + 22, wide, tall);
            /* Never reduced in a picture that has to be the same everywhere. */
            m.ground.setReducedMotionQuery ([] { return false; });

            addAndMakeVisible (m.panel);
            m.panel.setBounds (264, y + 22 + 30, 168, 60);
            m.ground.setWalls ({ m.panel.getBounds().toFloat() - m.ground.getPosition().toFloat() });
        }

        moments[1].play ({ { 1.0f, 0.0 } }, 600.0);
        const float beat = uv::tok::motion::beat::beat;
        moments[2].play ({ { 1.0f, 0.0 }, { beat, 500.0 }, { beat, 1000.0 }, { beat, 1500.0 }, { 1.0f, 2000.0 } }, 2500.0);
    }

    std::array<Moment, 3> moments;
};

} // namespace

NI_GALLERY_PAGE ("Display", "Ground", [] { return std::make_unique<GroundPage>(); });
