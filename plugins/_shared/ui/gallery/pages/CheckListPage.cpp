// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The CheckList as the Spectrogram's "view" uses it: its face at rest, under
 * the pointer, with keyboard focus and disabled; open, its rows switches with
 * their sample rates beside them, one refused at another rate and the row
 * under the pointer bg-300; and open with nothing to list.
 */
#include "Gallery.h"

#include "CheckList.h"
#include "ControlsPage.h"
#include "Info.h"
#include "Pointer.h"
#include "Toggle.h"

namespace
{
using ni::ui::CheckList;
using ni::ui::InfoText;

constexpr InfoText viewInfo { "View — the Listen-In buses drawn over this one, each in its colour." };

const std::vector<CheckList::Option> buses {
    { 1, "Kick", "48k" },
    { 2, "Bass", "48k" },
    { 3, "Pad", "44.1k", true },
    { 4, "Vocal", "48k" },
};

struct CheckListPage final : public ni::ui::gallery::ControlsPage
{
    CheckListPage()
    {
        heading ("CheckList: rest, hover, focus, disabled", 0, 0);
        add (rest, "Kick, Bass", 0, 24);
        add (hover, "Kick +3", 216, 24);
        add (focused, "nothing", 432, 24);
        add (off, "Kick", 0, 72);
        off.setEnabled (false);

        ni::ui::gallery::Pointer pointer;
        pointer.enter (*hover.getChildComponent (0), { 20.0f, 14.0f });
        showFocus (*focused.getChildComponent (0));

        heading ("open: switches, a bus refused at another rate", 0, 120);
        add (open, "Kick, Bass", 0, 144);
        heading ("open: nothing to list", 432, 120);
        add (empty, "none", 432, 144);
        empty.setOptions ({});

        setSize (680, 360);

        /* Open last, over the finished page: the panel is a layer of the
         * window, which outside an editor is this page. */
        open.open();
        empty.open();
        if (auto* vocal = open.getSwitch (4))
            pointer.enter (*vocal->getParentComponent(), { 40.0f, 14.0f });
    }

    void add (CheckList& c, const juce::String& summary, int x, int y)
    {
        c.setOptions (buses);
        c.setSelected ({ 1, 2 });
        c.setSummary (summary);
        c.setLabel ("View", 36);
        c.setFaceWidth (150);
        c.setEmptyText ("no Listen-In found");
        ni::ui::setInfo (c, viewInfo);
        c.setBounds (x, y, c.idealWidth(), (int) uv::tok::size::controlH);
        addAndMakeVisible (c);
    }

    CheckList rest, hover, focused, off, open, empty;
};

} // namespace

NI_GALLERY_PAGE ("Controls", "Check list", [] { return std::make_unique<CheckListPage>(); });
