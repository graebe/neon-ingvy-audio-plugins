// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A button and a group of them. Button.h has the states and the forms.
 */
#include "Button.h"

#include "ChildLights.h"
#include "UvIcons.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

#include <cmath>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;

constexpr float hair = uv::tok::stroke::strokeHair;
constexpr int controlH = (int) uv::tok::size::controlH;
constexpr int pad = (int) uv::tok::space::space3;     // padding: 0 var(--s3)
constexpr int gap = (int) uv::tok::space::space2;     // .btn { gap: var(--s2) }
constexpr int glyph = 16;                              // .icon { width: 16px }

/* The three colours a state is drawn in. */
struct Look
{
    juce::Colour fill, edge, ink;
};
} // namespace

/* ============================================================= button == */

Button::Button (const juce::String& t, const juce::String& i)
{
    setIcon (i);
    setText (t);
}

Button::~Button() = default;

void Button::setText (const juce::String& t)
{
    /* The word is the title a screen reader says, unless the caller named
     * the button otherwise (an icon-only button's verb, with setTitle). */
    if (getTitle().isEmpty() || getTitle() == text)
        setTitle (t);
    text = t;
    stateChanged();
}

void Button::setIcon (const juce::String& name)
{
    jassert (name.isEmpty() || uv::hasIcon (name));   // never a glyph outside the set
    icon = name;
    stateChanged();
}

void Button::setOn (bool shouldBeOn)
{
    const bool wasLatching = latching;
    latching = true;
    if (on == shouldBeOn && wasLatching)
        return;
    on = shouldBeOn;
    checkedChanged();
}

void Button::setPrimary (bool shouldBePrimary)
{
    if (primary == shouldBePrimary)
        return;
    primary = shouldBePrimary;
    stateChanged();
}

void Button::setContentLeft (bool shouldBeLeft)
{
    left = shouldBeLeft;
    repaint();
}

int Button::idealWidth() const
{
    if (isIconOnly())
        return controlH;

    const int words = text.isEmpty() ? 0 : (int) std::ceil (uv::type::width (uv::type::button(), text));
    const int lead = icon.isEmpty() ? 0 : glyph + (text.isEmpty() ? 0 : gap);
    return 2 * (int) hair + 2 * pad + lead + words;
}

bool Button::isLit() const
{
    return isEnabled() && (on || primary || isPressed());
}

bool Button::glows() const
{
    /* .ph-btn.primary and .ph-btn.icon.on carry glow-led; a pressed button
     * and a lit word do not. */
    return isEnabled() && (primary || (on && isIconOnly()));
}

void Button::pressed()
{
    if (onClick)
        onClick();   // may delete this
}

std::optional<bool> Button::checkedState() const
{
    return latching ? std::optional<bool> (on) : std::nullopt;
}

void Button::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    Look look { c::bg200, c::line200, c::ink };
    if (! isEnabled())     look = { c::bg100, c::line100, c::inkDim };
    else if (isLit())      look = { c::uv, c::uv, c::onUv };
    else if (isHovered())  look = { c::bg300, c::inkDim, c::ink };

    g.setColour (look.fill);
    g.fillRect (r);
    g.setColour (look.edge);
    g.drawRect (r, hair);

    /* The content: glyph, gap, word, as one run -- centred, or from the
     * left padding. */
    const auto font = uv::type::button();
    const float words = text.isEmpty() ? 0.0f : uv::type::width (font, text);
    const float lead = icon.isEmpty() ? 0.0f : (float) glyph + (text.isEmpty() ? 0.0f : (float) gap);
    const float run = lead + words;
    float x = left ? hair + (float) pad : r.getCentreX() - run * 0.5f;

    if (icon.isNotEmpty())
    {
        uv::drawIcon (g, icon, look.ink, { x, r.getY(), (float) glyph, r.getHeight() });
        x += lead;
    }

    /* A little more room than the word measures, so the last letter's
     * tracking never trips the ellipsis. */
    if (text.isNotEmpty())
        uv::type::draw (g, text, { x, r.getY(), words + 2.0f, r.getHeight() }, font, look.ink);
}

void Button::paintLight (juce::Graphics& g)
{
    if (glows())
    {
        juce::Graphics::ScopedSaveState state (g);
        excludeOwnBounds (g, *this);
        uv::light::glowLed (g, getLocalBounds().toFloat());
    }

    /* Focus over the glow: a focused lit button still shows its ring. */
    Pressable::paintLight (g);
}

/* ============================================================== group == */

ButtonGroup::ButtonGroup (Form f) : form (f)
{
}

ButtonGroup::~ButtonGroup() = default;

void ButtonGroup::add (Button& b)
{
    buttons.push_back (&b);
    addAndMakeVisible (b);
    /* Tab goes left to right, top to bottom, whatever is raised. */
    b.setExplicitFocusOrder ((int) buttons.size());
    b.setContentLeft (form == Form::stack);
    resized();
}

juce::Rectangle<int> ButtonGroup::idealSize() const
{
    if (buttons.empty())
        return {};

    const int hairline = (int) uv::tok::size::hairline;
    if (form == Form::joined)
    {
        int w = 0;
        for (auto* b : buttons)
            w += b->idealWidth() - hairline;
        return { w + hairline, controlH };
    }
    if (form == Form::column)
    {
        int w = 0;
        for (auto* b : buttons)
            w = juce::jmax (w, b->idealWidth());
        const int n = (int) buttons.size();
        return { w, n * (controlH - hairline) + hairline };
    }

    int w = 0;
    for (auto* b : buttons)
        w = juce::jmax (w, b->idealWidth());
    const int n = (int) buttons.size();
    return { w, n * controlH + (n - 1) * gap };
}

void ButtonGroup::resized()
{
    if (form == Form::joined)
    {
        /* .btn + .btn { margin-left: -1px }: each shares its left hairline
         * with the right one of the button before it. */
        int x = 0;
        for (auto* b : buttons)
        {
            const int w = b->idealWidth();
            b->setBounds (x, 0, w, controlH);
            x += w - (int) uv::tok::size::hairline;
        }
        restack();
        return;
    }

    if (form == Form::column)
    {
        /* The same sharing turned on its side: each button's top hairline is
         * the bottom one of the button above. */
        const int w = idealSize().getWidth();
        int y = 0;
        for (auto* b : buttons)
        {
            b->setBounds (0, y, w, controlH);
            y += controlH - (int) uv::tok::size::hairline;
        }
        restack();
        return;
    }

    const int w = idealSize().getWidth();
    int y = 0;
    for (auto* b : buttons)
    {
        b->setBounds (0, y, w, controlH);
        y += controlH + gap;
    }
}

bool ButtonGroup::raised (const Button& b) const
{
    /* .ph-actions.joined .ph-btn:hover, :focus-visible, and the transport's
     * .on: z-index 1. */
    return (b.isHovered() && b.isEnabled()) || b.isFocusShown() || b.isPressed() || b.isOn();
}

void ButtonGroup::restack()
{
    /* The rest in their order, then the raised ones in theirs: a raised
     * button's hairline is drawn over its neighbours'. */
    for (auto* b : buttons)
        if (! raised (*b))
            b->toFront (false);
    for (auto* b : buttons)
        if (raised (*b))
            b->toFront (false);
}

void ButtonGroup::paint (juce::Graphics& g)
{
    /* No ground of its own: the lights of the buttons, under them -- in a
     * stack, the gaps between them show it. */
    paintChildLights (g, *this);
}

void ButtonGroup::paintOverChildren (juce::Graphics& g)
{
    /* A raised button's light falls over its neighbours, which CSS paints
     * under it. Its light is all outside its own box, so painting it over
     * every child leaves the button itself untouched. */
    if (! isJoined())
        return;
    for (auto* b : buttons)
        if (raised (*b))
            paintChildLight (g, *this, *b);
}

void ButtonGroup::paintLight (juce::Graphics& g)
{
    forwardChildLights (g, *this);
}

void ButtonGroup::pressableStateChanged (Pressable&)
{
    if (isJoined())
        restack();
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> ButtonGroup::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}

} // namespace ni::ui
