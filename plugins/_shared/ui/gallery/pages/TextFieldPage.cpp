// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The TextField in its states, and the EditField open over the value it edits:
 * a value in a box, and a pad's arrival number in hint type.
 */
#include "Gallery.h"

#include "ControlsPage.h"
#include "EditField.h"
#include "Info.h"
#include "TextField.h"

namespace
{
using ni::ui::EditField;
using ni::ui::InfoText;
using ni::ui::TextField;

constexpr InfoText nameInfo { "Bus name — what the Spectrogram calls this bus; Enter keeps it." };
constexpr InfoText arrivalInfo { "Arrival — when this step comes in as Fade rises; type its place." };

struct TextFieldPage final : public ni::ui::gallery::ControlsPage
{
    TextFieldPage()
    {
        heading ("Text field: empty, named, typed into, disabled", 0, 0);
        const char* values[] = { "", "Bass & Kick", "Pads", "Bus 4" };
        for (int i = 0; i < 4; ++i)
        {
            auto& f = *fields.add (new TextField());
            f.setPlaceholder ("name this bus");
            f.setMaxLength (31);
            f.setValue (values[i]);
            f.setTitle ("Bus name");
            ni::ui::setInfo (f, nameInfo);
            f.setBounds (i * 160, 24, 136, (int) uv::tok::size::controlH);
            addAndMakeVisible (f);
        }
        fields[2]->focusGained (juce::Component::focusChangedByMouseClick);
        fields[3]->setEnabled (false);

        heading ("Edit field, open: a value, a pad's arrival", 0, 80);
        value.setBounds (0, 104, 64, (int) uv::tok::size::controlH);
        addAndMakeVisible (value);
        value.open ("40 ms");

        arrival.setBounds (96, 108, 20, 14);
        addAndMakeVisible (arrival);
        ni::ui::setInfo (arrival, arrivalInfo);
        arrival.open ("12");

        note ("open: the value selected, a uv hairline and glow-focus; Escape abandons", 0, 144);

        setSize (680, 168);
    }

    juce::OwnedArray<TextField> fields;
    EditField value;
    EditField arrival { uv::tok::type::hint };
};

} // namespace

NI_GALLERY_PAGE ("Controls", "Text field", [] { return std::make_unique<TextFieldPage>(); });
