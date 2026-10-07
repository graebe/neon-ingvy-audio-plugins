// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A fixed design, scaled to fit: the web kit's fit.js numbers, and the
 * transform that carries them, with the mouse following it.
 */
#include "Fit.h"

#include <doctest.h>

using namespace ni::ui;

TEST_CASE ("fit: the scale fits the design width into the viewport")
{
    CHECK (fitScale (412.0f, 824.0f) == doctest::Approx (0.5f));
    CHECK (fitScale (0.0f, 824.0f) == doctest::Approx (1.0f));      // no viewport yet
    CHECK (fitScale (10.0f, 824.0f) == doctest::Approx (0.1f));     // floored
    CHECK (scaledHeight (604.0f, 0.5f) == 302);
    CHECK (scaledHeight (101.0f, 1.5f) == 152);
    CHECK (scaledHeight (640.0f, 1.5f) == 960);                     // exact stays exact
}

TEST_CASE ("fit: the design keeps its size and is scaled from its top-left corner")
{
    juce::Component design;
    FixedDesign fixed (design, 824, 600);
    CHECK (fixed.getWidth() == 824);
    CHECK (fixed.getScale() == doctest::Approx (1.0f));

    fixed.setSize (412, 300);
    CHECK (fixed.getScale() == doctest::Approx (0.5f));
    CHECK (design.getBounds() == juce::Rectangle<int> (0, 0, 824, 600));
    CHECK (design.getTransform().mat00 == doctest::Approx (0.5f));

    /* A size out of proportion: the design stays whole inside it. */
    fixed.setSize (824, 300);
    CHECK (fixed.getScale() == doctest::Approx (0.5f));

    CHECK (fixed.boundsAt (1.5f) == juce::Rectangle<int> (0, 0, 1236, 900));
}

TEST_CASE ("fit: a design that grows is laid out at its new size, at the scale it had")
{
    juce::Component design;
    FixedDesign fixed (design, 824, 600);
    fixed.setSize (412, 300);

    fixed.setDesignSize (824, 648);
    CHECK (fixed.getDesignHeight() == 648);
    CHECK (design.getBounds() == juce::Rectangle<int> (0, 0, 824, 648));
    /* What the host is asked for next: the new height at the same scale. */
    CHECK (fixed.boundsAt (fixed.getScale()) == juce::Rectangle<int> (0, 0, 412, 324));
    fixed.setSize (412, 324);
    CHECK (fixed.getScale() == doctest::Approx (0.5f));
}

TEST_CASE ("fit: a click lands where the design drew the control, at any scale")
{
    juce::Component design;
    juce::Component control;
    design.addAndMakeVisible (control);
    control.setBounds (400, 300, 48, 48);

    FixedDesign fixed (design, 824, 600);
    fixed.setVisible (true);      // hit-testing only finds what is visible
    fixed.setSize (1648, 1200);   // twice the design

    /* The control's centre in design pixels is twice as far into the window. */
    CHECK (fixed.getComponentAt (848, 648) == &control);
    CHECK (fixed.getComponentAt (424, 324) != &control);
}

TEST_CASE ("fit: an editor's resize keeps the design's aspect")
{
    juce::ComponentBoundsConstrainer c;
    constrainToDesign (c, 824, 600, 0.5f, 2.0f);
    CHECK (c.getFixedAspectRatio() == doctest::Approx (824.0 / 600.0));
    CHECK (c.getMinimumWidth() == 412);
    CHECK (c.getMaximumHeight() == 1200);
}
