// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The LED as the Toggle card shows it: off, on, warn and clip, each with its
 * label beside it -- the four states a plugin can report with one lens.
 */
#include "DisplayPage.h"
#include "Gallery.h"

#include "Led.h"

#include <array>

namespace
{

using ni::ui::Led;

struct LedPage final : public ni::ui::gallery::DisplayPage
{
    static constexpr int column = 120, row = 28;

    LedPage()
    {
        struct State
        {
            const char* name;
            Led::Status status;
            const char* label;
        };
        const std::array<State, 4> states { { { "off", Led::Status::off, "Idle" },
                                              { "on", Led::Status::on, "Listening" },
                                              { "warn", Led::Status::warn, "Slot taken" },
                                              { "clip", Led::Status::clip, "Unavailable" } } };

        setSize ((int) states.size() * column, 22 + row);

        for (size_t i = 0; i < states.size(); ++i)
        {
            const int x = (int) i * column;
            caption (states[i].name, { (float) x, 0.0f, (float) column, 14.0f });
            auto& led = leds[i];
            led.setLabel (states[i].label);
            led.setStatus (states[i].status);
            addAndMakeVisible (led);
            led.setBounds (x + 8, 22, led.idealWidth(), row);
        }
    }

    std::array<Led, 4> leds;
};

} // namespace

NI_GALLERY_PAGE ("Display", "LED", [] { return std::make_unique<LedPage>(); });
