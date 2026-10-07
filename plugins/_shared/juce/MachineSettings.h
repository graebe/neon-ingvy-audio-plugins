// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What a person chose on this machine, for every Neon Ingvy plugin and never
 * in a host's set: today the Motion switch (EditorModel::motion).
 *
 * WHY NOT THE SET. Whether a background animates is a view of the window, not
 * a setting of the sound: a set opened on someone else's computer should move
 * as they asked theirs to (the web kit's lib/motion.js kept it in the
 * browser's storage for the same reason). So it is one file per user --
 * "Neon Ingvy/NI Plugins.settings" in the user's application data -- read by
 * every instance and written when the switch moves.
 *
 * Message thread.
 */
#pragma once

namespace ni
{

struct MachineSettings
{
    /* A product's Motion switch (its versions.json key, "trance-gate"): on
     * unless switched off for that product on this machine. Each product
     * remembers its own, as the manuals say. */
    static bool motion (const char* product);
    static void setMotion (const char* product, bool on);
};

} // namespace ni
