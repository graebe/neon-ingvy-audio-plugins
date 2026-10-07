// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Panel in its two forms, as its card shows them: two standard panels a
 * control group apart, their titles above the controls, and below them a
 * compact one, the title up its left edge. Readouts stand in for the
 * controls; the panel is what is pictured.
 */
#include "DisplayPage.h"
#include "Gallery.h"

#include "Info.h"
#include "Panel.h"
#include "Readout.h"

#include <memory>
#include <vector>

namespace
{

using ni::ui::Panel;

constexpr int cell = ni::ui::Readout::minWidth;
constexpr int gap = (int) uv::tok::space::space4;

struct PanelPage final : public ni::ui::gallery::DisplayPage
{
    PanelPage()
        : gate ("Gate"), envelope ("Envelope"), compact ("Gate", Panel::Form::compact)
    {
        setSize (640, 330);

        caption ("standard", { 0.0f, 0.0f, 200.0f, 14.0f });
        place (gate, { 0, 24 }, { "1/64", "32", "1.00" });
        place (envelope, { gate.getRight() + (int) uv::tok::space::space6, 24 },
               { "32 ms", "0 ms", "1.00", "72 ms" });

        caption ("compact", { 0.0f, 150.0f, 200.0f, 14.0f });
        place (compact, { 0, 174 }, { "1/64", "32", "1.00" });
        compact.setTitleInfo (juce::String::fromUTF8 (
            "Gate \xe2\x80\x94 when the steps fall and how much of each one sounds."));
    }

    void place (Panel& panel, juce::Point<int> at, std::vector<juce::String> values)
    {
        const int row = (int) values.size() * cell + ((int) values.size() - 1) * gap;
        const bool standard = panel.getForm() == Panel::Form::standard;
        /* Sized from the inside out: the controls, the title, the padding. */
        panel.setBounds (at.x, at.y, row + 2 * Panel::inset + (standard ? 0 : Panel::titleLine + Panel::titleGap),
                         standard ? 2 * Panel::inset + Panel::titleLine + Panel::titleGap + ni::ui::Readout::height
                                  : Panel::compactHeight);
        addAndMakeVisible (panel);

        const auto content = panel.contentBounds();
        int x = content.getX();
        for (const auto& v : values)
        {
            auto& r = *readouts.emplace_back (std::make_unique<ni::ui::Readout>());
            r.setValueText (v);
            panel.addAndMakeVisible (r);
            /* align-items: center in the compact form; from the top otherwise. */
            const int y = standard ? content.getY() : content.getCentreY() - ni::ui::Readout::height / 2;
            r.setBounds (x, y, cell, ni::ui::Readout::height);
            x += cell + gap;
        }
    }

    Panel gate, envelope, compact;
    std::vector<std::unique_ptr<ni::ui::Readout>> readouts;
};

} // namespace

NI_GALLERY_PAGE ("Display", "Panel", [] { return std::make_unique<PanelPage>(); });
