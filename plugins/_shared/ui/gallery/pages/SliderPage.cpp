// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Slider card: horizontal at rest, part way, full, focused and
 * disabled, and the vertical form.
 */
#include "Gallery.h"

#include "ControlsPage.h"
#include "Slider.h"

namespace
{
using ni::ui::Slider;

struct SliderPage final : public ni::ui::gallery::ControlsPage
{
    SliderPage()
    {
        heading ("Horizontal: empty, part way, full, focus, disabled", 0, 0);
        const float values[] { 0.0f, 0.6f, 1.0f, 0.35f, 0.5f };
        for (int i = 0; i < 5; ++i)
        {
            auto& s = *row.add (new Slider());
            s.setValue (values[i]);
            s.setBounds (i * 136, 24, 120, Slider::thickness);
            addAndMakeVisible (s);
        }
        showFocus (*row[3]);
        row[4]->setEnabled (false);

        heading ("Vertical", 0, 72);
        auto& v = *row.add (new Slider (Slider::Orientation::vertical));
        v.setValue (0.7f);
        v.setBounds (0, 96, Slider::thickness, 120);
        addAndMakeVisible (v);

        note ("a press jumps to the pointer; shift drags finer; arrows step", 48, 200);
        setSize (680, 224);
    }

    juce::OwnedArray<Slider> row;
};
} // namespace

NI_GALLERY_PAGE ("Controls", "Slider", [] { return std::make_unique<SliderPage>(); });
