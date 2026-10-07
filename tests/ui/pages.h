// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A gallery page as the gallery window shows it -- on the window ground,
 * right of the list -- rendered for a golden.
 *
 * The page is the component's every state side by side (Gallery.h), and the
 * ground under it is part of the picture: a light that reaches past its
 * control, a panel's hairline against the dot paper, a meter's glow on the
 * grain are what a person sees, so they are what the baseline holds.
 */
#pragma once

#include "Gallery.h"
#include "snapshot.h"

#include <functional>

namespace ni::ui::test
{

/* The page called `name`, after `prepare` has had it, held to `baseline`. */
inline SnapshotResult snapshotPage (const juce::String& name, const juce::String& baseline,
                                    const std::function<void (juce::Component&)>& prepare = {},
                                    const SnapshotOptions& options = {})
{
    gallery::Frame frame (gallery::pages());
    if (! frame.show (name) || frame.page() == nullptr)
        return { false, "no gallery page called " + name };

    if (prepare)
        prepare (*frame.page());

    const auto picture = frame.createComponentSnapshot (frame.page()->getBounds(), true, options.scale,
                                                        juce::SoftwareImageType());
    return compare (picture, baseline, options);
}

} // namespace ni::ui::test

/* Holds the gallery page `name` to the baseline `baseline`. */
#define NI_CHECK_PAGE(name, baseline)                                                      \
    do {                                                                                  \
        const auto niPage_ = ::ni::ui::test::snapshotPage ((name), (baseline));           \
        CHECK_MESSAGE (niPage_.ok, niPage_.message.toStdString());                        \
    } while (false)
