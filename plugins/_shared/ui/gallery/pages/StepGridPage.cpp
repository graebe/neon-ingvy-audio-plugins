// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A row of sixteen steps, each in another of the Step card's states, and the
 * two a fade adds: the border is what was drawn, the fill is what is heard.
 */
#include "Gallery.h"

#include "ControlsPage.h"
#include "Info.h"
#include "StepGrid.h"

#include <array>

namespace
{
using ni::ui::Step;
using ni::ui::StepGrid;

constexpr ni::ui::InfoText padsInfo { "Steps — click toggles, shift-click ties, drag sets the amount." };

struct StepGridPage final : public ni::ui::gallery::ControlsPage
{
    StepGridPage()
    {
        heading ("Step grid: on, partial, tie, and the playhead over each", 0, 0);
        heading ("What a fade and a mode add; accent, cursor, index", 0, 104);

        /* Two grids of eight, as the page is narrower than a row of sixteen. */
        for (auto* g : { &first, &second })
        {
            g->setCount (8);
            g->setSize (StepGrid::width, StepGrid::heightFor (8));
            g->setStepInfo (padsInfo);
            addAndMakeVisible (*g);
        }
        first.setTopLeftPosition (0, 32);
        second.setTopLeftPosition (0, 136);

        const auto drawn = [] (Step::Drawn d, float amount = 1.0f)
        {
            Step::State s;
            s.drawn = d;
            s.amount = amount;
            s.level = Step::levelAsDrawn (d);
            return s;
        };
        using D = Step::Drawn;
        std::array<Step::State, 16> states {
            drawn (D::off), drawn (D::on), drawn (D::on, 0.5f), drawn (D::tie),
            drawn (D::off), drawn (D::on), drawn (D::on, 0.5f), drawn (D::tie),
            drawn (D::on), drawn (D::off), drawn (D::on), drawn (D::off),
            drawn (D::off), drawn (D::on), drawn (D::off), drawn (D::on)
        };
        for (int i = 0; i < 16; ++i)
            states[(size_t) i].beat = i % 4 == 0;
        /* The playhead over each kind. */
        for (const int i : { 4, 5, 6, 7 })
            states[(size_t) i].play = true;
        /* A fade part way in: an on step it has not reached, one ramping in,
         * and a hole Fade Out has not removed yet. */
        states[8].level = 0.0f;
        states[10].level = 0.5f;
        states[11].level = 1.0f;
        /* A mode waiting on a step, an accent, the cursor, an index. */
        states[12].waiting = true;
        states[13].accent = true;
        states[14].cursor = true;
        states[15].number = 4;
        for (int i = 0; i < 8; ++i)
        {
            first.setState (i, states[(size_t) i]);
            second.setState (i, states[(size_t) i + 8]);
        }

        note ("off, on, 50 %, tie  |  the same under the playhead", 0, 80);
        note ("pending, off, ramping in, filled  |  waiting, accent, cursor, index", 0, 184);

        setSize (680, 208);
    }

    StepGrid first, second;
};

} // namespace

NI_GALLERY_PAGE ("Controls", "Step grid", [] { return std::make_unique<StepGridPage>(); });
