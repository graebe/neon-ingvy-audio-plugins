// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A strip of tabs. Tabs.h has how they look, how their light falls and which
 * keys they answer.
 */
#include "Tabs.h"

#include "ChildLights.h"
#include "Info.h"
#include "Keys.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;
constexpr float hair = uv::tok::stroke::strokeHair;
} // namespace

/* ================================================================ tab == */

class Tabs::Tab final : public Pressable
{
public:
    Tab (Tabs& o, int i, const juce::String& t)
        : Pressable (juce::AccessibilityRole::radioButton), owner (o), index (i), text (t)
    {
        setTitle (t);
    }

    bool isLit() const { return owner.getActive() == index; }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const bool lit = isLit();

        g.setColour (lit ? c::uv : (isHovered() ? c::bg300 : c::bg200));
        g.fillRect (r);
        g.setColour (lit ? c::uv : c::line200);
        g.drawRect (r, hair);

        /* writing-mode: vertical-rl, then a half turn: read bottom to top. */
        const auto& style = uv::tok::type::hint;
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi,
                                                         r.getCentreX(), r.getCentreY()));
        const auto turned = juce::Rectangle<float> (r.getHeight(), r.getWidth()).withCentre (r.getCentre());
        uv::type::draw (g, text.toUpperCase(), turned, uv::type::font (style),
                        lit ? c::onUv : c::inkMuted, juce::Justification::centred);
    }

    void paintLight (juce::Graphics& g) override
    {
        if (isLit())
        {
            juce::Graphics::ScopedSaveState state (g);
            excludeOwnBounds (g, *this);
            uv::light::glowLed (g, getLocalBounds().toFloat());
        }
        Pressable::paintLight (g);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (const auto to = keys::tabMove (keys::keyOf (key), index, owner.size()))
        {
            focus.keyUsed();
            owner.choose (*to, true);   // may delete this
            return true;
        }
        return Pressable::keyPressed (key);
    }

    /* After the owner lit it from the keys: the focus comes along, and
     * shows, as it would after a Tab. */
    void takeFocusFromKeys()
    {
        if (isShowing())
            grabKeyboardFocus();
        focus.keyUsed();
        stateChanged();
    }

    void refresh()
    {
        setWantsKeyboardFocus (isLit());   // one Tab stop: the lit tab
        checkedChanged();
    }

protected:
    void pressed() override { owner.choose (index, false); }
    std::optional<bool> checkedState() const override { return isLit(); }

private:
    Tabs& owner;
    const int index;
    const juce::String text;
};

/* =============================================================== tabs == */

Tabs::Tabs() = default;
Tabs::~Tabs() = default;

void Tabs::setTabs (const juce::StringArray& names, const juce::StringArray& infos)
{
    tabs.clear();
    for (int i = 0; i < names.size(); ++i)
    {
        auto* t = tabs.add (new Tab (*this, i, names[i]));
        if (i < infos.size())
            setInfo (*t, infos[i]);
        addAndMakeVisible (t);
    }
    active = juce::jlimit (0, juce::jmax (0, tabs.size() - 1), active);
    for (auto* t : tabs)
        t->refresh();
    resized();
}

void Tabs::setActive (int index)
{
    index = juce::jlimit (0, juce::jmax (0, tabs.size() - 1), index);
    if (index == active)
        return;
    active = index;
    for (auto* t : tabs)
        t->refresh();
    repaint();
    relight (*this);
}

Pressable& Tabs::getTab (int index) const
{
    return *tabs[index];
}

void Tabs::choose (int index, bool fromKeys)
{
    juce::Component::SafePointer<Tabs> self (this);
    if (index != active && onSelect)
        onSelect (index);   // the owner lights it, or not
    if (self != nullptr && fromKeys)
        tabs[active]->takeFocusFromKeys();
}

void Tabs::resized()
{
    /* flex: 1 each: equal shares of the height, the rounding spread over
     * them so they fill it exactly. */
    const int n = tabs.size();
    for (int i = 0; i < n; ++i)
    {
        const int top = juce::roundToInt ((float) (i * getHeight()) / (float) n);
        const int bottom = juce::roundToInt ((float) ((i + 1) * getHeight()) / (float) n);
        tabs[i]->setBounds (0, top, getWidth(), bottom - top);
    }
}

void Tabs::paintOverChildren (juce::Graphics& g)
{
    /* A tab's light over the tabs above it, which CSS painted before it; the
     * ones below cover it, as they paint after. Its light is all outside its
     * own box, so the tab itself is never touched. */
    for (int i = 1; i < tabs.size(); ++i)
    {
        auto* t = tabs[i];
        if (! t->isLit() && ! t->isFocusShown())
            continue;

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (juce::Rectangle<int> (0, 0, getWidth(), t->getY()));
        paintChildLight (g, *this, *t);
    }
}

void Tabs::paintLight (juce::Graphics& g)
{
    forwardChildLights (g, *this);
}

void Tabs::pressableStateChanged (Pressable&)
{
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> Tabs::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}

} // namespace ni::ui
