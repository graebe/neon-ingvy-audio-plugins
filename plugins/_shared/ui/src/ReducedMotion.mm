// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The system's reduced-motion setting on macOS. ReducedMotion.h has the rest.
 */
#include "ReducedMotion.h"

#import <AppKit/AppKit.h>

namespace ni::ui
{

bool systemReducesMotion()
{
    /* Accessibility > Display > Reduce motion; the property is current, so
     * a change made while an editor is open counts at once. */
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
}

} // namespace ni::ui
