// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Select in its states, a value cut with its ellipsis, and one open: its
 * list inside the window, the current option in uv, the row under the pointer
 * bg-300.
 */
#include "Gallery.h"

#include "ControlsPage.h"
#include "Info.h"
#include "Pointer.h"
#include "Select.h"

namespace
{
using ni::ui::InfoText;
using ni::ui::Select;

constexpr InfoText curveInfo { "Curve — the shape of each stage: linear, exponential or an S." };
constexpr InfoText timeInfo { "Time — the stages in milliseconds or in percent of a step." };
constexpr InfoText rateInfo { "Rate — the length of one step, synced to the song tempo." };
constexpr InfoText slotInfo { "Slot — which of the eight patterns plays; each keeps its sound." };

struct SelectPage final : public ni::ui::gallery::ControlsPage
{
    SelectPage()
    {
        heading ("Select: rest, hover, focus, disabled, cut", 0, 0);

        add (curve, "Curve", 44, 124, { "Linear", "Exponential", "S-Curve" }, 0, curveInfo, 0, 24);
        add (time, "Time", 36, 88, { "ms", "%" }, 0, timeInfo, 192, 24);
        add (dir, "Dir", 28, 76, { "In", "Out" }, 1, curveInfo, 340, 24);
        add (rate, "Rate", 38, 84, { "1/4", "1/8", "1/16" }, 2, rateInfo, 500, 24);
        add (cut, "Curve", 44, 96, { "Linear", "Exponential", "S-Curve" }, 1, curveInfo, 0, 72);
        add (slot, {}, 0, 80, { "1", "2", "3", "4", "5", "6", "7", "8" }, 0, slotInfo, 192, 72);
        slot.setTitle ("Slot");

        ni::ui::gallery::Pointer pointer;
        pointer.enter (time, time.field().getCentre().toFloat());
        showFocus (dir);
        rate.setEnabled (false);
        note ("a value too long for its field is cut, never wrapped", 300, 79);

        heading ("open: inside the window, under the field", 0, 120);
        add (open, "Rate", 38, 84, { "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T" }, 4, rateInfo, 0, 144);

        setSize (680, 360);

        /* Open last, over the finished page: its list is a layer of the
         * window, which outside an editor is this page. */
        open.open();
        if (auto* list = open.getList())
            list->setHighlighted (2);
    }

    void add (Select& s, const juce::String& label, int labelWidth, int width,
              const juce::StringArray& options, int index, const InfoText& info, int x, int y)
    {
        s.setOptions (options);
        s.setIndex (index);
        if (label.isNotEmpty())
            s.setLabel (label, labelWidth);
        s.setFieldWidth (width);
        ni::ui::setInfo (s, info);
        s.setBounds (x, y, s.idealWidth(), (int) uv::tok::size::controlH);
        addAndMakeVisible (s);
    }

    Select curve, time, dir, rate, cut, slot, open;
};

} // namespace

NI_GALLERY_PAGE ("Controls", "Select", [] { return std::make_unique<SelectPage>(); });
