// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Tab strips as the Trance Gate's band has one, 92px tall: each tab lit, one
 * under the pointer, one with the focus, and three to a strip -- where the lit
 * middle tab's light falls over the tab above it and under the one below.
 */
#include "Gallery.h"

#include "ControlsPage.h"
#include "Info.h"
#include "Pointer.h"
#include "Tabs.h"

namespace
{
using ni::ui::InfoText;
using ni::ui::Tabs;

constexpr InfoText patternInfo { "Pattern — the gate as programmed, one cycle across the plot." };
constexpr InfoText signalInfo { "Signal — what the gate did to the audio, dry behind it in grey." };
constexpr InfoText envelopeInfo { "Envelope — one step's attack, decay, sustain and release." };

struct TabsPage final : public ni::ui::gallery::ControlsPage
{
    TabsPage()
    {
        heading ("Tabs: lit, hover, focus, three", 0, 0);

        const juce::StringArray two { "Pattern", "Signal" };
        const juce::StringArray twoInfos { patternInfo.str(), signalInfo.str() };
        for (int i = 0; i < 3; ++i)
        {
            auto& t = *strips.add (new Tabs());
            t.setTabs (two, twoInfos);
            t.setBounds (i * 72, 32, Tabs::width, 92);
            addAndMakeVisible (t);
        }
        strips[1]->setActive (1);
        ni::ui::gallery::Pointer pointer;
        pointer.enter (strips[1]->getTab (0));
        showFocus (strips[2]->getTab (0));

        auto& three = *strips.add (new Tabs());
        three.setTabs ({ "Pattern", "Signal", "Envelope" },
                       { patternInfo.str(), signalInfo.str(), envelopeInfo.str() });
        three.setActive (1);
        three.setBounds (216, 32, Tabs::width, 168);
        addAndMakeVisible (three);

        note ("a lit tab's light falls over the tab above it, under the one below", 0, 216);

        setSize (680, 240);
    }

    juce::OwnedArray<Tabs> strips;
};

} // namespace

NI_GALLERY_PAGE ("Controls", "Tabs", [] { return std::make_unique<TabsPage>(); });
