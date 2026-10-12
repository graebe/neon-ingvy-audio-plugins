// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Filter Designer's window. Editor.h has its layout and its rules.
 */
#include "Editor.h"

#include "InfoLines.h"

#include "ChildLights.h"
#include "Info.h"
#include "Knob.h"
#include "Luminous.h"
#include "UvTokens.h"
#include "UvType.h"

namespace ni::fd
{

namespace
{
namespace c = uv::tok::colour;

constexpr int knobGap = (int) uv::tok::space::space2;
constexpr int rowGap = (int) uv::tok::space::space3;
constexpr int rowH = (int) uv::tok::size::controlH;
constexpr int labelH = 14;
constexpr int labelGap = (int) uv::tok::space::space2;
constexpr int selectGap = (int) uv::tok::space::space4;

/* The four selects' widths across the inspector. */
constexpr std::array<int, 4> selectWidths { 126, 150, 96, 90 };
} // namespace

/* --------------------------------------------------------------- view -- */

/* The content: lets its controls' light reach past it. */
class Editor::View final : public juce::Component,
                           public ni::ui::Luminous
{
public:
    View() { setTitle ("NI Filter Designer"); }

    void paint (juce::Graphics& g) override { ni::ui::paintChildLights (g, *this); }
    void paintLight (juce::Graphics& g) override { ni::ui::forwardChildLights (g, *this); }
};

/* A select's label over it, in the label style: ink-muted, ink-dim while
 * what it names is disabled. */
class Editor::Caption final : public juce::Component
{
public:
    explicit Caption (const juce::String& t) : text (t) { setInterceptsMouseClicks (false, false); }

    void setLit (bool on)
    {
        if (on != lit)
        {
            lit = on;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (uv::type::label());
        g.setColour (lit ? c::inkMuted : c::inkDim);
        g.drawText (uv::type::cased (uv::tok::type::label, text), getLocalBounds(), juce::Justification::centredLeft);
    }

private:
    juce::String text;
    bool lit = true;
};

/* ------------------------------------------------------------- editor -- */

Editor::Editor (Model& m, ni::ui::EditorFrame::Clock clock)
    : model (m),
      window (m, std::move (clock)),
      view (std::make_unique<View>()),
      response (std::make_unique<ResponseWell> (m)),
      nodes (std::make_unique<NodeRow> (m)),
      inspectorPanel (std::make_unique<ni::ui::Panel> ("Node 1")),
      outPanel (std::make_unique<ni::ui::Panel> ("Output"))
{
    view->addAndMakeVisible (*response);
    view->addAndMakeVisible (*nodes);
    view->addAndMakeVisible (*inspectorPanel);
    view->addAndMakeVisible (*outPanel);
    response->onSelect = [this] (int s) { select (s); };
    nodes->onSelect = [this] (int s) { select (s); };

    const auto knob = [this] (int index, const char* label, const juce::String& line, const juce::String& value)
    {
        auto k = std::make_unique<ni::ui::ParamKnob> (model.parameter (index), label);
        k->setBipolar (model.bipolar (index));
        ni::ui::setInfo (*k, line);
        k->setReadoutInfo (value);
        return k;
    };
    outKnobs.push_back (knob (param::output, "Output", info::output, info::dbValue));
    outKnobs.push_back (knob (param::scale, "Scale", info::scale, info::plainValue));
    for (auto& k : outKnobs)
        outPanel->addAndMakeVisible (*k);
    adaptive = std::make_unique<ni::ui::ParamToggle> (model.parameter (param::adaptiveQ), "Adaptive");
    ni::ui::setInfo (*adaptive, info::adaptive);
    outPanel->addAndMakeVisible (*adaptive);

    /* THE WINDOW. */
    window.setConventions ({ { "double-click", "to add a filter" }, { "drag", "it, alt for Q" }, { "delete", "removes it" } });
    window.setMotionInfo (info::motion);
    window.setSignatureInfo (info::signature);
    window.setContent (view.get());
    addAndMakeVisible (scaler);

    window.setSize (designWidth, designHeight);
    rebuildInspector();
    layout();
    tick (window.frameClock().now());

    frames = window.frameClock().subscribe ([this] (double now) { tick (now); });
    setSize (designWidth, designHeight);
}

Editor::~Editor()
{
    frames.reset();
}

juce::Component& Editor::content() noexcept
{
    return *view;
}

void Editor::select (int s)
{
    s = juce::jlimit (0, (int) ResponseWell::isolator, s);
    if (s == selection)
        return;
    selection = s;
    response->setSelected (s);
    nodes->setSelected (s);
    rebuildInspector();
    layout();
    refreshInspector();
}

ni::ui::ParamKnob* Editor::knobFor (int index)
{
    for (auto& k : knobs)
        if (k->binding().parameter().getParameterIndex() == index)
            return k.get();
    for (auto& k : outKnobs)
        if (k->binding().parameter().getParameterIndex() == index)
            return k.get();
    return nullptr;
}

ni::ui::ParamSelect* Editor::selectFor (int index)
{
    for (auto& s : selects)
        if (s->binding().parameter().getParameterIndex() == index)
            return s.get();
    return nullptr;
}

ni::ui::ParamToggle* Editor::toggleFor (int index)
{
    for (auto& t : toggles)
        if (t->binding().parameter().getParameterIndex() == index)
            return t.get();
    if (adaptive->binding().parameter().getParameterIndex() == index)
        return adaptive.get();
    return nullptr;
}

/* The inspector's controls, bound to the selection's parameters. */
void Editor::rebuildInspector()
{
    knobs.clear();
    selects.clear();
    captions.clear();
    toggles.clear();
    shownFor = { -1, -1, -1 };

    const auto addKnob = [this] (int index, const char* label, const juce::String& line, const juce::String& value, bool choice = false)
    {
        std::unique_ptr<ni::ui::ParamKnob> k;
        if (choice)
            k = std::make_unique<ni::ui::ParamChoiceKnob> (model.parameter (index), label);
        else
            k = std::make_unique<ni::ui::ParamKnob> (model.parameter (index), label);
        k->setBipolar (model.bipolar (index));
        ni::ui::setInfo (*k, line);
        k->setReadoutInfo (value);
        inspectorPanel->addAndMakeVisible (*k);
        knobs.push_back (std::move (k));
    };
    const auto addToggle = [this] (int index, const char* label, const juce::String& line)
    {
        auto t = std::make_unique<ni::ui::ParamToggle> (model.parameter (index), label);
        ni::ui::setInfo (*t, line);
        inspectorPanel->addAndMakeVisible (*t);
        toggles.push_back (std::move (t));
    };

    if (selection == ResponseWell::isolator)
    {
        inspectorPanel->setTitleText ("Isolator");
        addKnob (param::isoLow, "Low", info::low, info::dbValue);
        addKnob (param::isoMid, "Mid", info::mid, info::dbValue);
        addKnob (param::isoHigh, "High", info::high, info::dbValue);
        addKnob (param::isoLower, "Lower", info::lower, info::hzValue);
        addKnob (param::isoUpper, "Upper", info::upper, info::hzValue);
        addKnob (param::isoSlope, "Slope", info::slope, info::plainValue, true);
        addToggle (param::isoKillLow, "Kill", info::kill);
        addToggle (param::isoKillMid, "Kill", info::kill);
        addToggle (param::isoKillHigh, "Kill", info::kill);
        addToggle (param::isoOn, "On", info::isoOn);
        return;
    }

    const int s = selection;
    const struct
    {
        int field;
        const char* label;
        ni::ui::InfoText line;
    } choices[] = {
        { param::type, "Type", info::type },
        { param::family, "Family", info::family },
        { param::order, "Order", info::order },
        { param::channel, "Channel", info::channel },
    };
    for (const auto& ch : choices)
    {
        auto sel = std::make_unique<ni::ui::ParamSelect> (model.parameter (param::node (s, ch.field)));
        ni::ui::setInfo (*sel, ch.line);
        inspectorPanel->addAndMakeVisible (*sel);
        selects.push_back (std::move (sel));
        auto cap = std::make_unique<Caption> (ch.label);
        inspectorPanel->addAndMakeVisible (*cap);
        captions.push_back (std::move (cap));
    }
    addKnob (param::node (s, param::freq), "Freq", info::freq, info::hzValue);
    addKnob (param::node (s, param::gain), "Gain", info::gain, info::dbValue);
    addKnob (param::node (s, param::q), "Q", info::q, info::plainValue);
    addKnob (param::node (s, param::ripple), "Ripple", info::ripple, info::dbValue);
    addKnob (param::node (s, param::stopband), "Stopband", info::stopband, info::dbValue);
    addToggle (param::node (s, param::on), "On", info::on);
}

/* What the selected type uses, lit; the rest disabled (fd_type_uses). */
void Editor::refreshInspector()
{
    const auto plain = [this] (int index)
    {
        auto& p = model.parameter (index);
        return (int) std::lround (p.convertFrom0to1 (p.getValue()));
    };
    if (selection == ResponseWell::isolator)
    {
        inspectorPanel->setTitleText ("Isolator");
        return;
    }
    const int s = selection;
    const int t = plain (param::node (s, param::type));
    const int order = plain (param::node (s, param::order));
    const int family = plain (param::node (s, param::family));
    if (shownFor == std::array<int, 3> { t, order, family })
        return;
    shownFor = { t, order, family };

    static const char* const names[] = { "Empty", "Bell", "Low Shelf", "High Shelf", "Low Cut",
                                         "High Cut", "Band Pass", "Notch", "All Pass", "Tilt" };
    inspectorPanel->setTitleText ("Node " + juce::String (s + 1) + "   " + names[juce::jlimit (0, 9, t)]);
    const unsigned used = model.typeUses (t, order);
    const bool empty = t == type::empty;
    const bool familyLit = (used & uses::family) != 0;
    const bool ripple = familyLit && model.familyUsesRipple (family);
    const bool stop = familyLit && model.familyUsesStopband (family);
    const bool lit[] = { true, familyLit, (used & uses::order) != 0, ! empty };
    for (size_t i = 0; i < selects.size(); ++i)
    {
        selects[i]->setEnabled (lit[i]);
        captions[i]->setLit (lit[i]);
    }
    const bool cut = t == type::lowCut || t == type::highCut;
    if (auto* k = knobFor (param::node (s, param::q)))
    {
        k->setLabel (cut ? "Resonance" : "Q");
        ni::ui::setInfo (*k, cut ? info::resonance : info::q);
        k->setEnabled ((used & uses::q) != 0);
    }
    if (auto* k = knobFor (param::node (s, param::freq)))
        k->setEnabled (! empty);
    if (auto* k = knobFor (param::node (s, param::gain)))
        k->setEnabled ((used & uses::gain) != 0);
    if (auto* k = knobFor (param::node (s, param::ripple)))
        k->setEnabled (ripple);
    if (auto* k = knobFor (param::node (s, param::stopband)))
        k->setEnabled (stop);
    if (auto* on = toggleFor (param::node (s, param::on)))
        on->setEnabled (! empty);
}

void Editor::layout()
{
    response->setBounds (0, 0, outputX + outputW, wellH);
    nodes->setBounds (0, rowY, outputX + outputW, NodeRow::cell);
    inspectorPanel->setBounds (0, panelsY, inspectorW, panelH);
    outPanel->setBounds (outputX, panelsY, outputW, panelH);

    const auto area = inspectorPanel->contentBounds();
    /* On, at the title's right. */
    for (auto& t : toggles)
    {
        const int index = t->binding().parameter().getParameterIndex();
        if (index == param::isoOn || (selection < ResponseWell::isolator && index == param::node (selection, param::on)))
            t->setBounds (area.getRight() - t->idealWidth(), area.getY() - ni::ui::Panel::titleGap - ni::ui::Panel::titleLine - 6,
                          t->idealWidth(), rowH);
    }
    if (selection == ResponseWell::isolator)
    {
        const int cell = (area.getWidth() - 5 * knobGap) / 6;
        for (size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (area.getX() + (int) i * (cell + knobGap), area.getY(), cell, ni::ui::Knob::cardHeight);
        const int y = area.getY() + ni::ui::Knob::cardHeight + rowGap;
        for (size_t i = 0; i < 3; ++i)
        {
            const int w = toggles[i]->idealWidth();
            toggles[i]->setBounds (area.getX() + (int) i * (cell + knobGap) + (cell - w) / 2, y, w, rowH);
        }
    }
    else
    {
        int x = area.getX();
        for (size_t i = 0; i < selects.size(); ++i)
        {
            captions[i]->setBounds (x, area.getY(), selectWidths[i], labelH);
            selects[i]->setFieldWidth (selectWidths[i]);
            selects[i]->setBounds (x, area.getY() + labelH + labelGap, selectWidths[i], rowH);
            x += selectWidths[i] + selectGap;
        }
        const int y = area.getY() + labelH + labelGap + rowH + rowGap;
        const int cell = (area.getWidth() - 4 * knobGap) / 5;
        for (size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (area.getX() + (int) i * (cell + knobGap), y, cell, ni::ui::Knob::cardHeight);
    }

    const auto out = outPanel->contentBounds();
    const int cell = (out.getWidth() - 2 * knobGap) / 3;
    for (size_t i = 0; i < outKnobs.size(); ++i)
        outKnobs[i]->setBounds (out.getX() + (int) i * (cell + knobGap), out.getY(), cell, ni::ui::Knob::cardHeight);
    adaptive->setBounds (out.getX(), out.getY() + ni::ui::Knob::cardHeight + rowGap, adaptive->idealWidth(), rowH);
}

void Editor::tick (double)
{
    response->update();
    nodes->update();
    refreshInspector();
}

void Editor::resized()
{
    scaler.setBounds (getLocalBounds());
    layout();
}

} // namespace ni::fd
