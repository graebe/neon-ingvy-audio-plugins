// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editable ring: sixteen steps with the cursor and the playhead, and
 * the band at 128, narrowed from the inside, with the keyboard's focus.
 */
#include "Gallery.h"

#include "ControlsPage.h"
#include "Info.h"
#include "Ring.h"

namespace
{
using ni::ui::Ring;

constexpr ni::ui::InfoText ringInfo { "Ring — click a wedge to toggle its step, sweep to paint." };

struct RingPage final : public ni::ui::gallery::ControlsPage
{
    RingPage()
    {
        heading ("Ring: on, partial, tie, cursor, playhead; 128 steps with focus", 0, 0);

        const auto setUp = [this] (Ring& r, int n, int x)
        {
            r.setCount (n);
            r.setCentre (juce::String (n), "Steps");
            r.setTopLeftPosition (x, 32);
            ni::ui::setInfo (r, ringInfo);
            addAndMakeVisible (r);
        };
        setUp (sixteen, 16, 0);
        setUp (long128, 128, 288);

        for (int i = 0; i < 16; i += 2)
            sixteen.setStep (i, { true, false, i == 6 ? 0.4f : 1.0f });
        sixteen.setStep (9, { false, true, 1.0f });
        sixteen.setCursor (2);
        sixteen.setPlayhead (5, true);

        for (int i = 0; i < 128; i += 3)
            long128.setStep (i, { true, false, 1.0f });
        long128.setPlayhead (40, true);
        showFocus (long128);

        setSize (528 + 24, 32 + Ring::defaultSize + 16);
    }

    Ring sixteen, long128;
};

} // namespace

NI_GALLERY_PAGE ("Controls", "Ring", [] { return std::make_unique<RingPage>(); });
