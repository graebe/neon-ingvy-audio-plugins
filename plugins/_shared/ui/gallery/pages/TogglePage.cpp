// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Toggle's switch in each of its states, and the switches the editors
 * carry, at their own widths.
 */
#include "Gallery.h"

#include "ControlsPage.h"
#include "Info.h"
#include "Pointer.h"
#include "Toggle.h"

namespace
{
using ni::ui::InfoText;
using ni::ui::Toggle;

constexpr InfoText joinInfo { "Join Neighbors — run each step on into the next one that is on." };
constexpr InfoText softInfo { "Soft — the fade raises each step's amount instead of switching it." };
constexpr InfoText motionInfo { "Motion — let the ground ring on the beat; off keeps it still." };

struct TogglePage final : public ni::ui::gallery::ControlsPage
{
    TogglePage()
    {
        heading ("Switch: off, on, hover, disabled, disabled on, focus", 0, 0);
        const char* labels[] = { "Join Neighbors", "Soft", "bars", "clash", "48k", "Motion" };
        const InfoText* infos[] = { &joinInfo, &softInfo, &joinInfo, &joinInfo, &joinInfo, &motionInfo };
        int x = 0;
        for (int i = 0; i < 6; ++i)
        {
            auto& t = *row.add (new Toggle (labels[i]));
            ni::ui::setInfo (t, *infos[i]);
            t.setBounds (x, 24, t.idealWidth(), (int) uv::tok::size::controlH);
            addAndMakeVisible (t);
            x += t.idealWidth() + 24;
        }
        row[1]->setOn (true);
        ni::ui::gallery::Pointer pointer;
        pointer.enter (*row[2], { 14.0f, 14.0f });   // over the housing
        row[3]->setEnabled (false);
        row[4]->setEnabled (false);
        row[4]->setOn (true);
        row[5]->setOn (true);
        showFocus (*row[5]);

        note ("hover raises the housing only, not the label; disabled is a refused row", 0, 64);

        setSize (680, 88);
    }

    juce::OwnedArray<Toggle> row;
};

} // namespace

NI_GALLERY_PAGE ("Controls", "Toggle", [] { return std::make_unique<TogglePage>(); });
