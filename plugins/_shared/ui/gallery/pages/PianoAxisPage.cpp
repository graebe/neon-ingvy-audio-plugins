// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The PianoAxis beside the pitch rows it lines up with, at three zooms: close
 * enough to name every C and mark C major's keys, with E and G lit; further
 * out, the held keys dimmed; and the whole of MIDI, where the keys are too
 * thin for names or marks.
 */
#include "Gallery.h"

#include "DisplayPage.h"
#include "PianoAxis.h"
#include "PitchView.h"

namespace
{
using namespace ni::ui;

constexpr std::uint16_t cMajor = 0b1010'1011'0101;

struct PianoAxisPage final : public gallery::DisplayPage
{
    PianoAxisPage()
    {
        const char* captions[] = { "C major, E and G lit", "Held: dimmed", "All of MIDI" };
        const std::pair<double, double> views[] = { { 55.5, 73.5 }, { 35.5, 84.5 }, { -0.5, 127.5 } };
        for (int i = 0; i < 3; ++i)
        {
            const int x = i * 228;
            caption (captions[i], { (float) x, 0.0f, 220.0f, 14.0f });

            zoom::Axis pitch;
            pitch.lo = 0.0;
            pitch.hi = 127.0;
            pitch.show (views[i].first, views[i].second);

            auto& keys = *axes.add (new PianoAxis());
            keys.setBounds (x, 24, PianoAxis::defaultWidth, 400);
            keys.setAxis (pitch);
            keys.setScale (cMajor, 0);
            addAndMakeVisible (keys);

            auto& view = *rows.add (new PitchView());
            view.setBounds (x + PianoAxis::defaultWidth, 24, 188, 400);
            view.setAxis (pitch);
            view.setScale (cMajor);
            addAndMakeVisible (view);
        }

        music::NoteSet lit;
        lit.set (64);
        lit.set (67);
        axes[0]->setLit (lit);
        axes[1]->setLit (lit);
        axes[1]->setDimmed (true);
        setSize (680, 424);
    }

    juce::OwnedArray<PianoAxis> axes;
    juce::OwnedArray<PitchView> rows;
};

} // namespace

NI_GALLERY_PAGE ("Music", "Piano Axis", [] { return std::make_unique<PianoAxisPage>(); });
