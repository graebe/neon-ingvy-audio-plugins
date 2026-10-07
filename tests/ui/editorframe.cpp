// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The window every editor is drawn in: its layout (the padding, the content
 * from the top, the bar on the bottom edge), the Motion switch as the model's,
 * the Ground fed from the model and walled by the content, the bar's info and
 * an outcome that leaves on time, and the light of the bar over the Ground.
 */
#include "EditorFrame.h"

#include "Panel.h"
#include "UvGround.h"
#include "UvTokens.h"
#include "WaveSource.h"
#include "checks.h"
#include "events.h"
#include "fakes.h"
#include "pages.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::Clause;
using ni::ui::EditorFrame;
using ni::ui::Hint;

namespace
{
const juce::String rate = juce::String::fromUTF8 ("Rate \xe2\x80\x94 the length of one step, synced to the song tempo.");

/* A frame on a clock the test moves, at the Spectrogram's size. */
struct Rig
{
    double ms = 0.0;
    ni::ui::test::FakeEditorModel model;
    EditorFrame frame { model, [this] { return ms; } };

    Rig()
    {
        frame.ground().setReducedMotionQuery ([] { return false; });
        frame.setSize (720, 502);
    }

    void frames (int n)
    {
        for (int k = 0; k < n; ++k)
        {
            ms += 1000.0 / 30.0;
            frame.ground().tick();
        }
    }
};
} // namespace

TEST_CASE ("editor frame: padding space-8, content from the top, the bar on the bottom edge")
{
    Rig rig;
    juce::Component content;
    rig.frame.setContent (&content);

    CHECK (rig.frame.ground().getBounds() == juce::Rectangle<int> (0, 0, 720, 502));
    /* 502 = 32 + 402 + 24 + 28 + 16 */
    CHECK (content.getBounds() == juce::Rectangle<int> (32, 32, 656, 402));
    CHECK (rig.frame.contentBounds() == content.getBounds());
    CHECK (rig.frame.hint().getBounds() == juce::Rectangle<int> (0, 458, 720, Hint::height));
    CHECK (EditorFrame::heightFor (402) == 502);

    /* A window that grows (the Trance Gate's rows): the content grows with it,
     * the bar stays on the bottom edge. */
    rig.frame.setSize (720, 550);
    CHECK (content.getHeight() == 450);
    CHECK (rig.frame.hint().getBottom() == 550 - EditorFrame::bottomPadding);

    /* The Ground is behind everything, the bar in front of the content. */
    CHECK (rig.frame.getIndexOfChildComponent (&rig.frame.ground()) == 0);
    auto* layer = content.getParentComponent();
    REQUIRE (layer != nullptr);
    CHECK (layer->getIndexOfChildComponent (&content) < layer->getIndexOfChildComponent (&rig.frame.hint()));

    rig.frame.setContent (nullptr);
    CHECK (content.getParentComponent() == nullptr);
}

TEST_CASE ("editor frame: the Motion switch shows the model's state and asks the model to change it")
{
    Rig rig;
    auto& motion = rig.frame.motionSwitch();
    CHECK (motion.isOn());
    CHECK (rig.frame.ground().isEnabled());
    CHECK (motion.getParentComponent() == &rig.frame.hint());

    ni::ui::test::click (motion, { 7.0f, ni::ui::test::centreOf (motion).y });
    CHECK_FALSE (rig.model.motionOn);
    CHECK_FALSE (motion.isOn());
    CHECK_FALSE (rig.frame.ground().isEnabled());

    /* Changed elsewhere: the frame shows it when told. */
    rig.model.motionOn = true;
    rig.frame.motionChanged();
    CHECK (motion.isOn());
    CHECK (rig.frame.ground().isEnabled());

    /* A model that remembered Off opens with it off. */
    ni::ui::test::FakeEditorModel off;
    off.motionOn = false;
    EditorFrame frame (off);
    CHECK_FALSE (frame.motionSwitch().isOn());
    CHECK_FALSE (frame.ground().isEnabled());
}

TEST_CASE ("editor frame: the model's rings move the Ground, broken by the content's boxes")
{
    Rig rig;
    juce::Component content;
    ni::ui::Panel panel ("Gate");
    content.addAndMakeVisible (panel);
    rig.frame.setContent (&content);
    panel.setBounds (100, 40, 200, 140);

    rig.model.rings.push_back (1.0f);
    rig.frames (1);
    CHECK (rig.model.rings.empty());
    CHECK (rig.frame.ground().isMoving());

    /* The panel is a wall, where it is in the window. */
    const auto& walls = rig.frame.ground().getField().getWalls();
    REQUIRE (walls.size() == 1);
    CHECK (walls[0] == juce::Rectangle<float> (132.0f, 72.0f, 200.0f, 140.0f));

    /* Motion off: rings counted meanwhile are not played. */
    ni::ui::test::click (rig.frame.motionSwitch(), { 7.0f, 14.0f });
    CHECK_FALSE (rig.frame.ground().isMoving());
    rig.model.rings.push_back (1.0f);
    rig.frames (2);
    CHECK_FALSE (rig.frame.ground().isMoving());
}

TEST_CASE ("editor frame: the bar shows the info line, then an outcome for six seconds")
{
    Rig rig;
    rig.frame.setConventions ({ { "click", "a step to toggle" }, { "drag", "for its amount" } });
    auto content = [&] { return rig.frame.hint().content(); };

    CHECK_FALSE (content().info.has_value());
    rig.frame.infoState().enter (rate);
    REQUIRE (content().info.has_value());
    CHECK (content().info->name == "Rate");

    /* Out after the grace, by the clock. */
    rig.frame.infoState().leave();
    rig.ms += ni::ui::infoGraceMs + 1.0;
    rig.frame.poll();
    CHECK_FALSE (content().info.has_value());

    rig.frame.showOutcome ({ "Copied", "slot 1." });
    CHECK (content().clauses.front() == Clause { "Copied", "slot 1." });
    CHECK (content().clauses.size() == 3);

    rig.ms += EditorFrame::outcomeMs - 1.0;
    rig.frame.poll();
    CHECK (rig.frame.outcome().has_value());

    /* A late timer leaves it up a little longer, never wrongly: the outcome
     * is a function of the clock. */
    rig.ms += 2.0;
    CHECK_FALSE (rig.frame.outcome().has_value());
    rig.frame.poll();
    CHECK (content().clauses.front() == Clause { "click", "a step to toggle" });

    /* A second outcome restarts the time; clearing takes it away at once. */
    rig.frame.showOutcome ({ "Pasted", "slot 2." });
    rig.ms += 4000.0;
    rig.frame.showOutcome ({ "Copied", "slot 3." });
    rig.ms += 4000.0;
    CHECK (rig.frame.outcome().has_value());
    rig.frame.clearOutcome();
    CHECK_FALSE (rig.frame.outcome().has_value());
    CHECK (content().clauses.front().name == "click");
}

TEST_CASE ("editor frame: the bar's two lines, within the limit, and the window takes the keyboard round")
{
    Rig rig;
    /* The kit's own two lines until an editor says otherwise. */
    CHECK (ni::ui::infoOf (rig.frame.motionSwitch()) == ni::ui::EditorFrame::motionInfo.str());
    CHECK (ni::ui::infoOf (rig.frame.hint().signature()) == ni::ui::EditorFrame::signatureInfo.str());
    NI_CHECK_INFO_LIMIT (rig.frame);

    rig.frame.setMotionInfo (juce::String::fromUTF8 ("Motion \xe2\x80\x94 let the music ripple the background."));
    rig.frame.setSignatureInfo (juce::String::fromUTF8 ("Neon Ingvy \xe2\x80\x94 the publisher of this plugin."));
    CHECK (ni::ui::infoOf (rig.frame.motionSwitch()).startsWith ("Motion"));
    CHECK (ni::ui::infoOf (rig.frame.hint().signature()).startsWith ("Neon Ingvy"));
    NI_CHECK_INFO_LIMIT (rig.frame);

    CHECK (rig.frame.isKeyboardFocusContainer());
    CHECK (ni::ui::InfoHost::find (rig.frame.motionSwitch()) == &rig.frame);
    CHECK (rig.frame.frameClock().now() == doctest::Approx (rig.ms));
}

TEST_CASE ("editor frame: the signature's light falls over the Ground")
{
    Rig rig;
    const auto img = ni::ui::test::render (rig.frame);

    juce::Image ground (juce::Image::ARGB, 720, 502, true, juce::SoftwareImageType());
    {
        juce::Graphics g (ground);
        uv::ground::paint (g, { 0.0f, 0.0f, 720.0f, 502.0f });
    }

    /* Just above the bar's rule, over the signature's mark: glow-led reaches
     * out of the bar onto the window's ground. */
    const auto& sig = rig.frame.hint().signature();
    const auto mark = sig.getBounds().translated (0, rig.frame.hint().getY());
    const int x = mark.getX() + 3, y = rig.frame.hint().getY() - 3;
    CHECK (img.getPixelAt (x, y).getBrightness() > ground.getPixelAt (x, y).getBrightness() + 0.01f);

    /* Far from anything that glows, it is the ground. */
    CHECK (img.getPixelAt (360, 200) == ground.getPixelAt (360, 200));
}

NI_SNAPSHOT_TEST ("editor frame: at rest, and with Motion off and an outcome")
{
    NI_CHECK_PAGE ("display-editor-frame", "editor-frame");
}
