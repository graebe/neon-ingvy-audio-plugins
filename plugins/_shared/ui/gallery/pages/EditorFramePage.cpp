// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The window every editor is drawn in, 640 x 200, twice: at rest, with a
 * panel of content, the conventions and Motion on; and with Motion off and an
 * action's outcome in the first clause's place. The padding, the bar on the
 * bottom edge and the Signature closing it are the same in both, as they are
 * in every window.
 */
#include "DisplayPage.h"
#include "Gallery.h"

#include "EditorFrame.h"
#include "Panel.h"

#include <array>

namespace
{

using namespace ni::ui;

constexpr int wide = 640, tall = 200;

/* A model with one parameter and no transport: the frame asks it for Motion
 * and for rings, and the page has none to give. */
struct PageModel final : public EditorModel
{
    juce::AudioParameterFloat level { juce::ParameterID { "level", 1 }, "Level", 0.0f, 1.0f, 0.5f };
    bool on = true;

    int numParameters() const override { return 1; }
    juce::RangedAudioParameter& parameter (int) override { return level; }
    int takeRings (float*, int) override { return 0; }
    bool motion() const override { return on; }
    void setMotion (bool m) override { on = m; }
};

struct Window
{
    PageModel model;
    EditorFrame frame { model, [] { return 0.0; } };
    Panel panel { "Bus" };
};

struct EditorFramePage final : public gallery::DisplayPage
{
    static constexpr int row = 22 + tall + 16;

    EditorFramePage()
    {
        setSize (wide, 2 * row - 16);
        const char* names[] { "at rest", "motion off, an outcome" };
        for (int i = 0; i < 2; ++i)
        {
            auto& w = windows[(size_t) i];
            caption (names[i], { 0.0f, (float) (i * row), (float) wide, 14.0f });
            w.frame.setConventions ({ { "click", "a bus to listen" },
                                      { "drag", "for the level" },
                                      { "double-click", "to reset" } });
            w.frame.setSize (wide, tall);
            w.frame.setContent (&w.panel);
            /* Never reduced in a picture that has to be the same everywhere. */
            w.frame.ground().setReducedMotionQuery ([] { return false; });
            addAndMakeVisible (w.frame);
            w.frame.setTopLeftPosition (0, i * row + 22);
        }

        windows[1].model.on = false;
        windows[1].frame.motionChanged();
        windows[1].frame.showOutcome ({ "Copied", "slot 1." });
    }

    std::array<Window, 2> windows;
};

} // namespace

NI_GALLERY_PAGE ("Display", "Editor frame", [] { return std::make_unique<EditorFramePage>(); });
