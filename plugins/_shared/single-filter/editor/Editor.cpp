// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A single filter's window. Editor.h has its layout and its rules.
 */
#include "Editor.h"

#include "InfoLines.h"

#include "ChildLights.h"
#include "Info.h"
#include "Knob.h"
#include "Luminous.h"
#include "UvTokens.h"
#include "UvType.h"

namespace ni::sf
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
constexpr int switchGap = (int) uv::tok::space::space6;
} // namespace

/* --------------------------------------------------------------- view -- */

/* The content: lets its controls' light reach past it. */
class Editor::View final : public juce::Component,
                           public ni::ui::Luminous
{
public:
    explicit View (const juce::String& title) { setTitle (title); }

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
      window (std::move (clock)),
      view (std::make_unique<View> ("NI " + m.productName())),
      response (std::make_unique<ResponseWell> (m)),
      filterPanel (std::make_unique<ni::ui::Panel> (m.productName()))
{
    view->addAndMakeVisible (*response);
    view->addAndMakeVisible (*filterPanel);
    const bool allPass = m.shape() == shape::allPass;
    const bool cut = m.shape() == shape::highPass || m.shape() == shape::lowPass;
    const bool shelf = m.shape() == shape::lowShelf || m.shape() == shape::highShelf;

    /* THE SELECTS, each as wide as its longest option: "Linkwitz-Riley",
     * "48 dB/oct", "Stereo". */
    const struct
    {
        int field;
        int width;
        const char* label;
        ni::ui::InfoText line;
    } selectSpecs[] = {
        { field::family, 152, "Family", info::family },
        { field::order, 114, allPass ? "Order" : "Slope", allPass ? info::order : info::slope },
        { field::channel, 100, "Channel", info::channel },
    };
    for (const auto& spec : selectSpecs)
    {
        const int index = m.indexOf (spec.field);
        if (index < 0)
            continue;
        Choice ch { spec.field, spec.width, std::make_unique<ni::ui::ParamSelect> (m.parameter (index)), std::make_unique<Caption> (spec.label) };
        ni::ui::setInfo (*ch.control, spec.line);
        filterPanel->addAndMakeVisible (*ch.control);
        filterPanel->addAndMakeVisible (*ch.caption);
        choices.push_back (std::move (ch));
    }
    /* A choice of Slope or Family lands on the order that plays
     * (sf_order_played): a Linkwitz-Riley cut's is even. So the field shows
     * what is heard. */
    const auto pick = [] (ni::ui::ParamSelect& sel, int index)
    {
        const int last = sel.getOptions().size() - 1;
        sel.binding().commit (last > 0 ? (float) index / (float) last : 0.0f);
    };
    if (auto* order = selectFor (field::order))
    {
        auto* family = selectFor (field::family);
        order->onChange = [this, order, family, pick] (int index)
        {
            pick (*order, model.orderPlayed (family != nullptr ? family->indexOfValue() : 0, index));
        };
        if (family != nullptr)
            family->onChange = [this, order, family, pick] (int index)
            {
                pick (*family, index);
                const int played = model.orderPlayed (index, order->indexOfValue());
                if (played != order->indexOfValue())
                    pick (*order, played);
            };
    }

    /* THE KNOBS, in Filter Designer's order; Output last. */
    const struct
    {
        int field;
        const char* label;
        ni::ui::InfoText line;
        ni::ui::InfoText value;
    } knobSpecs[] = {
        { field::freq, "Freq", info::freq, info::hzValue },
        { field::gain, "Gain", info::gain, info::dbValue },
        { field::q, "Q", cut ? info::passQ : shelf ? info::shelfQ : allPass ? info::allPassQ : info::q, info::plainValue },
        { field::ripple, "Ripple", info::ripple, info::dbValue },
        { field::stopband, "Stopband", info::stopband, info::dbValue },
        { field::output, "Output", info::output, info::dbValue },
    };
    for (const auto& spec : knobSpecs)
    {
        const int index = m.indexOf (spec.field);
        if (index < 0)
            continue;
        auto k = std::make_unique<ni::ui::ParamKnob> (m.parameter (index), spec.label);
        k->setBipolar (m.bipolar (index));
        ni::ui::setInfo (*k, spec.line);
        k->setReadoutInfo (spec.value);
        filterPanel->addAndMakeVisible (*k);
        knobs.push_back ({ spec.field, std::move (k) });
    }

    /* THE SWITCHES at the title's right: the type's, then Bypass. */
    if (m.indexOf (field::matched) >= 0)
        switchField = field::matched;
    else if (m.indexOf (field::adaptiveQ) >= 0)
        switchField = field::adaptiveQ;
    if (switchField >= 0)
    {
        const bool matched = switchField == field::matched;
        typeSwitch = std::make_unique<ni::ui::ParamToggle> (m.parameter (m.indexOf (switchField)), matched ? "Matched" : "Adaptive");
        ni::ui::setInfo (*typeSwitch, matched ? info::matched : info::adaptive);
        filterPanel->addAndMakeVisible (*typeSwitch);
    }
    bypassSwitch = std::make_unique<ni::ui::ParamToggle> (m.bypassParameter(), "Bypass");
    ni::ui::setInfo (*bypassSwitch, info::bypass);
    filterPanel->addAndMakeVisible (*bypassSwitch);

    /* THE WINDOW. */
    const bool gainType = m.indexOf (field::gain) >= 0;
    window.setConventions ({ { "drag", gainType ? "the dot, alt for Q" : "the dot: freq and Q" },
                             { "shift", "for fine" },
                             { "double-click", "resets" } });
    window.setSignatureInfo (info::signature);
    window.setContent (view.get());
    addAndMakeVisible (scaler);

    window.setSize (designWidth, designHeight);
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

ni::ui::ParamKnob* Editor::knobFor (int f)
{
    for (auto& k : knobs)
        if (k.field == f)
            return k.control.get();
    return nullptr;
}

ni::ui::ParamSelect* Editor::selectFor (int f)
{
    for (auto& ch : choices)
        if (ch.field == f)
            return ch.control.get();
    return nullptr;
}

ni::ui::ParamToggle* Editor::toggleFor (int f)
{
    return f == switchField ? typeSwitch.get() : nullptr;
}

int Editor::plain (int f) const
{
    const int index = model.indexOf (f);
    if (index < 0)
        return 0;
    auto& p = model.parameter (index);
    return (int) std::lround (p.convertFrom0to1 (p.getValue()));
}

/* What the product uses now, lit; the rest it has disabled (sf_uses). */
void Editor::refresh()
{
    const int order = plain (field::order), family = plain (field::family);
    if (shownFor == std::array<int, 2> { order, family })
        return;
    shownFor = { order, family };
    const unsigned used = model.uses (order, family);
    if (auto* k = knobFor (field::q))
        k->setEnabled ((used & uses::q) != 0);
    if (auto* k = knobFor (field::ripple))
        k->setEnabled ((used & uses::ripple) != 0);
    if (auto* k = knobFor (field::stopband))
        k->setEnabled ((used & uses::stopband) != 0);
}

void Editor::layout()
{
    const int width = designWidth - 64;
    response->setBounds (0, 0, width, wellH);
    filterPanel->setBounds (0, panelY, width, panelH);

    const auto area = filterPanel->contentBounds();
    /* The switches, at the title's right: Bypass last. */
    const int titleY = area.getY() - ni::ui::Panel::titleGap - ni::ui::Panel::titleLine - 6;
    int right = area.getRight();
    bypassSwitch->setBounds (right - bypassSwitch->idealWidth(), titleY, bypassSwitch->idealWidth(), rowH);
    right -= bypassSwitch->idealWidth() + switchGap;
    if (typeSwitch != nullptr)
        typeSwitch->setBounds (right - typeSwitch->idealWidth(), titleY, typeSwitch->idealWidth(), rowH);

    int x = area.getX();
    for (auto& ch : choices)
    {
        ch.caption->setBounds (x, area.getY(), ch.width, labelH);
        ch.control->setFieldWidth (ch.width);
        ch.control->setBounds (x, area.getY() + labelH + labelGap, ch.width, rowH);
        x += ch.width + selectGap;
    }
    const int y = area.getY() + labelH + labelGap + rowH + rowGap;
    const int cell = (area.getWidth() - (columns - 1) * knobGap) / columns;
    int column = 0;
    for (auto& k : knobs)
    {
        /* Output against the right edge, whatever the rounding left. */
        const int left = k.field == field::output ? area.getRight() - cell : area.getX() + column++ * (cell + knobGap);
        k.control->setBounds (left, y, cell, ni::ui::Knob::cardHeight);
    }
}

void Editor::tick (double)
{
    response->update();
    refresh();
}

void Editor::resized()
{
    scaler.setBounds (getLocalBounds());
    layout();
}

} // namespace ni::sf
