// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Every native UI test program's main: JUCE's GUI started, the kit's
 * LookAndFeel the default, then doctest.
 *
 * JUCE FIRST, because a Component needs the message manager, a Typeface needs
 * the font machinery, and both are JUCE's to start and to stop -- in that
 * order around everything a test makes. The thread that runs main() is the
 * message thread, so a test may do anything an editor does.
 *
 * THE KIT'S LOOKANDFEEL THE DEFAULT, because JUCE asks the default one which
 * face a font names: with it, any font a widget makes for itself is an
 * embedded face (UvLookAndFeel.h), so no snapshot can pick up whatever is
 * installed on the machine running it. It is released before JUCE stops.
 *
 * No window and no display: nothing here opens a peer. A test that needs a
 * timer to fire runs the message loop itself
 * (juce::MessageManager::runDispatchLoopUntil); most inject a clock instead.
 *
 *   ni_ui_tests                              everything
 *   ni_ui_tests --test-suite-exclude=snapshot   the quick tier's unit tests
 *   ni_ui_tests --test-suite=snapshot           the full tier's goldens
 *   ni_ui_tests --test-case='*info*'            by name
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "UvLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;

    int result = 0;
    {
        juce::SharedResourcePointer<uv::SharedLookAndFeel> look;

        doctest::Context context (argc, argv);
        result = context.run();
    }
    return result;
}
