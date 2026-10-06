// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Signature as its card shows it: at its size, and at twice its size for
 * inspection only ("Do not enlarge it" -- in a window it is never scaled).
 */
#include "DisplayPage.h"
#include "Gallery.h"

#include "Signature.h"

namespace
{

struct SignaturePage final : public ni::ui::gallery::DisplayPage
{
    SignaturePage()
    {
        setSize (440, 96);

        caption ("1x", { 0.0f, 0.0f, 120.0f, 14.0f });
        addAndMakeVisible (atSize);
        atSize.setTopLeftPosition (0, 40);

        caption ("2x, inspection only", { 160.0f, 0.0f, 260.0f, 14.0f });
        addAndMakeVisible (twice);
        twice.setTopLeftPosition (0, 0);
        twice.setTransform (juce::AffineTransform::scale (2.0f).translated (160.0f, 33.0f));
    }

    ni::ui::Signature atSize, twice;
};

} // namespace

NI_GALLERY_PAGE ("Display", "Signature", [] { return std::make_unique<SignaturePage>(); });
