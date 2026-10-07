// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The window every editor is drawn in. EditorFrame.h has the design.
 */
#include "EditorFrame.h"

#include "Luminous.h"
#include "TextField.h"

namespace ni::ui
{

namespace
{
EditorFrame::Clock orNow (EditorFrame::Clock c)
{
    return c ? std::move (c) : EditorFrame::Clock ([] { return juce::Time::getMillisecondCounterHiRes(); });
}
} // namespace

/* --------------------------------------------------------------- layer -- */

EditorFrame::Layer::Layer()
{
    /* Transparent, and no target of its own: a press on the window's
     * background lands on nothing, as it does on the web page's. */
    setInterceptsMouseClicks (false, true);
}

void EditorFrame::Layer::paint (juce::Graphics& g)
{
    paintChildLights (g, *this);
}

/* ------------------------------------------------------------ presses -- */

EditorFrame::PressListener::PressListener (EditorFrame& f) : frame (f)
{
    frame.addMouseListener (this, true);
}

EditorFrame::PressListener::~PressListener()
{
    frame.removeMouseListener (this);
}

void EditorFrame::PressListener::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent != nullptr)
        frame.pressed (*e.eventComponent);
}

namespace
{
/* The TextField under `c` with an edit open, if any: one at most, as only
 * one has the keyboard. */
TextField* editedIn (juce::Component& c)
{
    for (auto* child : c.getChildren())
    {
        if (auto* field = dynamic_cast<TextField*> (child); field != nullptr && field->isBeingEdited())
            return field;
        if (auto* field = editedIn (*child))
            return field;
    }
    return nullptr;
}
} // namespace

/* --------------------------------------------------------------- frame -- */

EditorFrame::EditorFrame (EditorModel& m, Clock c)
    : model (m),
      clock (orNow (std::move (c))),
      groundLayer (clock),
      info (clock),
      frames (*this, clock)
{
    /* Tab goes round the window's controls, and stays in the window. */
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    addAndMakeVisible (groundLayer);
    addAndMakeVisible (layer);
    layer.addAndMakeVisible (bar);

    groundLayer.setSourceRoot (&layer);
    groundLayer.setRingSource ([this] (float* strengths, int capacity)
                               { return model.takeRings (strengths, capacity); });

    motionToggle.setSize (motionToggle.idealWidth(), Hint::height);
    motionToggle.onChange = [this] (bool on)
    {
        model.setMotion (on);
        motionChanged();
    };
    setInfo (motionToggle, motionInfo);
    setInfo (bar.signature(), signatureInfo);
    bar.setMotionSwitch (&motionToggle);
    bar.setPadding (padding);

    info.onChange = [this] { refreshBar(); };
    motionChanged();
}

EditorFrame::~EditorFrame()
{
    stopTimer();
    /* The bar holds the switch as a child; the switch goes first. */
    bar.setMotionSwitch (nullptr);
}

void EditorFrame::setContent (juce::Component* c)
{
    if (content != nullptr && content->getParentComponent() == &layer)
        layer.removeChildComponent (content);
    content = c;
    if (content != nullptr)
    {
        /* Under the bar in z-order: the bar's light falls over the content's
         * padding, never the other way round. */
        layer.addAndMakeVisible (*content, 0);
        content->setBounds (contentBounds());
    }
    groundLayer.setSourceRoot (&layer);
}

juce::Rectangle<int> EditorFrame::contentBounds() const
{
    const int bottom = getHeight() - bottomPadding - Hint::height - barGap;
    return { padding, padding, juce::jmax (0, getWidth() - 2 * padding), juce::jmax (0, bottom - padding) };
}

void EditorFrame::setConventions (std::vector<Clause> conventions)
{
    bar.setConventions (std::move (conventions));
}

void EditorFrame::showOutcome (Clause c)
{
    shownOutcome = std::move (c);
    outcomeAt = clock();
    refreshBar();
}

void EditorFrame::clearOutcome()
{
    shownOutcome.reset();
    refreshBar();
}

std::optional<Clause> EditorFrame::outcome() const
{
    if (shownOutcome && clock() - outcomeAt < outcomeMs)
        return shownOutcome;
    return std::nullopt;
}

void EditorFrame::setMotionInfo (const juce::String& line)
{
    setInfo (motionToggle, line);
}

void EditorFrame::setSignatureInfo (const juce::String& line)
{
    setInfo (bar.signature(), line);
}

void EditorFrame::motionChanged()
{
    const bool on = model.motion();
    motionToggle.setOn (on);
    groundLayer.setEnabled (on);
}

void EditorFrame::pressed (juce::Component& at)
{
    /* `at` has had its press and taken the keyboard if it takes it; a field
     * still typed into was pressed past -- unless the press was on it. */
    if (auto* field = editedIn (*this); field != nullptr && field != &at && ! field->isParentOf (&at))
        field->finishEdit();   // its commit may delete it
}

void EditorFrame::poll()
{
    info.poll();
    refreshBar();
}

void EditorFrame::refreshBar()
{
    const auto now = outcome();
    if (! now)
        shownOutcome.reset();
    bar.setOutcome (now);
    bar.setInfoLine (info.text());

    /* The one timer this needs: to take the outcome away when its time is
     * up. A late tick leaves it up a little longer, never wrongly. */
    if (now)
        startTimer (juce::jmax (1, (int) std::ceil (outcomeMs - (clock() - outcomeAt))));
    else
        stopTimer();
}

void EditorFrame::resized()
{
    const auto all = getLocalBounds();
    groundLayer.setBounds (all);
    layer.setBounds (all);
    bar.setBounds (0, getHeight() - bottomPadding - Hint::height, getWidth(), Hint::height);
    if (content != nullptr)
        content->setBounds (contentBounds());
}

} // namespace ni::ui
