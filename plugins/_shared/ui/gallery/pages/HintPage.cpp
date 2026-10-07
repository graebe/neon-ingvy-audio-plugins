// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Hint as its card shows it, three times over the same conventions: at
 * rest; with the pointer on Length, its line laid over the conventions; and
 * after a copy, the outcome in the first clause's place. Each with the Motion
 * switch and the Signature, which stand where they stand in all three -- and
 * a narrow bar last, where the tips give way and the 16px before the
 * Signature does not.
 */
#include "DisplayPage.h"
#include "Gallery.h"

#include "Hint.h"
#include "Toggle.h"

#include <array>

namespace
{

using ni::ui::Clause;
using ni::ui::Hint;

const juce::String length = juce::String::fromUTF8 (
    "Length \xe2\x80\x94 1 to 128 steps; holds on bar lengths, Page Up/Down jumps.");

struct HintPage final : public ni::ui::gallery::DisplayPage
{
    static constexpr int width = 680, narrow = 400, step = 52;

    HintPage()
    {
        setSize (width, 4 * step);

        const std::vector<Clause> conventions { { "click", "a step to toggle" },
                                                { "shift-click", "for a tie" },
                                                { "drag", "up or down for its amount" } };
        const char* names[] = { "conventions", "info: pointer on Length", "outcome: after copy",
                                "narrow: the tips give way" };

        for (int i = 0; i < 4; ++i)
        {
            auto& bar = bars[(size_t) i];
            auto& motion = switches[(size_t) i];
            caption (names[i], { 16.0f, (float) (i * step), 400.0f, 14.0f });

            motion.setLabel ("Motion");
            motion.setOn (true);
            motion.setSize (motion.idealWidth(), (int) uv::tok::size::controlH);

            bar.setPadding ((int) uv::tok::space::space4);
            bar.setConventions (conventions);
            bar.setMotionSwitch (&motion);
            addAndMakeVisible (bar);
            bar.setBounds (0, i * step + 18, i == 3 ? narrow : width, Hint::height);
        }

        bars[1].setInfoLine (length);
        bars[2].setOutcome (Clause { "Copied", "slot 1." });
    }

    std::array<ni::ui::Toggle, 4> switches;
    std::array<Hint, 4> bars;
};

} // namespace

NI_GALLERY_PAGE ("Display", "Hint", [] { return std::make_unique<HintPage>(); });
