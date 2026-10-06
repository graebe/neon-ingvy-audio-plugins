// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Meter as its card shows it: live at a working level, live and quiet,
 * and off -- measured, going nowhere -- at a level of its own. Empty and full
 * close the set, so both ends of the fill are pictured too.
 */
#include "DisplayPage.h"
#include "Gallery.h"

#include "Meter.h"

#include <array>

namespace
{

struct MeterPage final : public ni::ui::gallery::DisplayPage
{
    static constexpr int width = 360, label = 64, row = 28;

    MeterPage()
    {
        struct State
        {
            const char* name;
            float level;
            bool live;
        };
        const std::array<State, 5> states { { { "live", 0.62f, true }, { "quiet", 0.18f, true },
                                              { "off", 0.45f, false }, { "empty", 0.0f, true },
                                              { "full", 1.0f, true } } };

        setSize (label + width, (int) states.size() * row);

        for (size_t i = 0; i < states.size(); ++i)
        {
            const int y = (int) i * row;
            caption (states[i].name, { 0.0f, (float) y + 7.0f, (float) label, 14.0f });
            auto& m = meters[i];
            m.setLevel (states[i].level);
            m.setLive (states[i].live);
            m.setTitle ("Input level");
            addAndMakeVisible (m);
            m.setBounds (label, y + (row - ni::ui::Meter::height) / 2, width, ni::ui::Meter::height);
        }
    }

    std::array<ni::ui::Meter, 5> meters;
};

} // namespace

NI_GALLERY_PAGE ("Display", "Meter", [] { return std::make_unique<MeterPage>(); });
