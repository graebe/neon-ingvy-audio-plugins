// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Checks every editor's tests make the same way.
 *
 *   NI_CHECK_INFO_LIMIT (editor);
 *
 * holds every info line in the tree under `editor` to the Hint's 72
 * characters (Info.h). Run it on each state an editor shows -- a line that
 * follows a mode (the pads' "click the steps in the order..." while ORDER is
 * on) is only in the tree while the mode is.
 */
#pragma once

#include "Info.h"

#include <doctest.h>

#define NI_CHECK_INFO_LIMIT(root)                                             \
    do {                                                                     \
        for (const auto& niLine_ : ::ni::ui::collectInfo (root))             \
        {                                                                    \
            CAPTURE (niLine_);                                               \
            CHECK (niLine_.length() <= ::ni::ui::infoLimit);                 \
        }                                                                    \
    } while (false)
