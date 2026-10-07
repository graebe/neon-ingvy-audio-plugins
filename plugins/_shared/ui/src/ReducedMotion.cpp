// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The system's reduced-motion setting, where it is not Cocoa's. ReducedMotion.h
 * has which setting each platform's is; macOS's is in ReducedMotion.mm.
 */
#include "ReducedMotion.h"

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

#if ! JUCE_MAC

namespace ni::ui
{

bool systemReducesMotion()
{
   #if JUCE_WINDOWS
    /* "Show animations in Windows", which is what the Settings app's switch
     * writes and what Edge maps prefers-reduced-motion onto. */
    BOOL animate = TRUE;
    if (SystemParametersInfoW (SPI_GETCLIENTAREAANIMATION, 0, &animate, 0))
        return animate == FALSE;
    return false;
   #else
    return false;
   #endif
}

} // namespace ni::ui

#endif
