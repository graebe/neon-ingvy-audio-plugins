// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Whether the person at this computer has asked for less motion -- the
 * system's own setting, which the web editors read as the CSS media query
 * prefers-reduced-motion and JUCE does not expose.
 *
 * "prefers-reduced-motion turns it off regardless" (Ground README): the
 * Ground asks this whenever it would start moving, so a setting changed in
 * the middle of a session counts from the next ring, and the Motion switch
 * cannot turn it back on.
 *
 *   macOS    NSWorkspace.accessibilityDisplayShouldReduceMotion
 *            (System Settings > Accessibility > Display > Reduce motion);
 *            ReducedMotion.mm
 *   Windows  "Show animations in Windows" off: SPI_GETCLIENTAREAANIMATION
 *   Linux    no system-wide setting a plugin can read without a desktop
 *            toolkit (GTK's gtk-enable-animations lives in GSettings), so
 *            false: the Motion switch is the way to stop it there.
 *
 * Message thread.
 */
#pragma once

namespace ni::ui
{

bool systemReducesMotion();

} // namespace ni::ui
