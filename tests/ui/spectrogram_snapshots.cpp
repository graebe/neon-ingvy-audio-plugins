// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Spectrogram's window in its main states, held to goldens: just opened
 * with nothing arriving (the amber "no signal"); live, looking at two
 * channels added, with the crosshair reading a point; and the bar view,
 * paused, with the clash marked between the two compared buses.
 *
 * The music is made up here, deterministically, in the shape the layouts
 * draw it: a low rumble, a kick on every beat with its click above it, hats
 * high up between them, and a lead line over four notes -- sent through the
 * fake model in batches, as the plugin hands them over.
 */
#include "SpectrogramEditor.h"

#include "Pointer.h"
#include "snapshot.h"
#include "spectrogram_fake.h"

#include <doctest.h>

#include <cmath>

using namespace ni::spectrogram;
using test::FakeModel;

namespace
{
constexpr int bands = 256;

/* One column of the made-up music, band 0 lowest; and where it clashes. */
void music (int t, std::uint8_t* out, std::uint8_t* clash)
{
    const int beat = 22;
    const int inBeat = t % beat;
    const int note = (t / (beat * 2)) % 4;
    static constexpr int lead[] { 140, 132, 124, 132 };

    for (int b = 0; b < bands; ++b)
    {
        float v = juce::jmax (0.0f, 30.0f - (float) b * 0.3f);
        if (b < 28)
            v += 40.0f + 8.0f * std::sin ((float) (t + b) * 0.3f);
        if (inBeat < 4 && b < 60)
            v += 190.0f - (float) inBeat * 35.0f - (float) b * 1.5f;
        if (inBeat < 2 && b >= 72 && b < 88)
            v += 110.0f;
        if (inBeat >= 11 && inBeat < 13 && b > 192 && b < 240)
            v += 90.0f;
        for (int h = 1; h <= 3; ++h)
        {
            const int d = std::abs (b - (lead[note] + (h - 1) * 28));
            if (d < 3)
                v += (h == 1 ? 150.0f : 95.0f / (float) h) - (float) d * 25.0f;
        }
        out[b] = (std::uint8_t) juce::jlimit (0, 255, juce::roundToInt (v));
        const bool clashes = t > 300 && inBeat < 6 && b >= 8 && b < 52;
        clash[b] = clashes ? (std::uint8_t) juce::jlimit (0, 255, 230 - inBeat * 25 - std::abs (b - 30) * 4) : 0;
    }
}

struct Rig
{
    double ms = 1000.0;
    FakeModel model;
    std::unique_ptr<SpectrogramEditor> editor;

    Rig()
    {
        model.buses = { { 1, true, 48000, "bass" }, { 2, true, 48000, "pad" } };
        model.clock.bpm = 128.0;
        model.clock.running = true;
        model.clock.ppqPerColumn = 128.0 / 60.0 / 47.0;
    }

    void open()
    {
        editor = std::make_unique<SpectrogramEditor> (model, [this] { return ms; });
        editor->frame().ground().setReducedMotionQuery ([] { return false; });
    }

    /* `columns` of music, 32 to a frame, the transport moving with them. */
    void play (int columns, bool withClash)
    {
        for (int t = 0; t < columns; t += Model::maxColumns)
        {
            const int n = juce::jmin (Model::maxColumns, columns - t);
            std::vector<std::uint8_t> levels ((size_t) (n * bands)), clash ((size_t) (n * bands));
            for (int c = 0; c < n; ++c)
                music (t + c, levels.data() + c * bands, clash.data() + c * bands);
            model.clock.ppq += n * model.clock.ppqPerColumn;
            model.queue (std::move (levels), withClash ? std::move (clash) : std::vector<std::uint8_t> {}, n);
            ms += n * 1000.0 / 47.0;
            editor->frame().frameClock().tick();
        }
    }
};
} // namespace

NI_SNAPSHOT_TEST ("spectrogram: just opened, nothing arriving")
{
    Rig rig;
    rig.open();
    NI_CHECK_SNAPSHOT (*rig.editor, "spectrogram-no-signal");
}

NI_SNAPSHOT_TEST ("spectrogram: live, two channels added, the crosshair on a kick")
{
    Rig rig;
    rig.model.look.view = { 0, 1 };
    rig.open();
    rig.play (640, false);

    auto& picture = rig.editor->view().picture();
    const auto at = juce::Point<float> (420.5f, 220.5f);
    picture.mouseEnter (ni::ui::gallery::Pointer::event (picture, at));
    picture.mouseMove (ni::ui::gallery::Pointer::event (picture, at));
    NI_CHECK_SNAPSHOT (*rig.editor, "spectrogram-live");
}

NI_SNAPSHOT_TEST ("spectrogram: the bar view, paused, the clash marked between two buses")
{
    Rig rig;
    rig.model.look.compareA = 1;
    rig.model.look.compareB = 2;
    rig.model.look.clash = true;
    rig.open();
    auto& v = rig.editor->view();
    v.barsSwitch().onChange (true);
    v.barCountSelect().onChange (1);
    rig.play (640, true);
    v.pauseButton().onClick();
    NI_CHECK_SNAPSHOT (*rig.editor, "spectrogram-bars-clash");
}
