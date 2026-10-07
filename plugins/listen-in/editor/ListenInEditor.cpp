// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In's window. ListenInEditor.h has the layout and the rules.
 */
#include "ListenInEditor.h"

#include "ChildLights.h"
#include "InfoLines.h"
#include "UvTokens.h"

#include <cmath>

namespace ni::li
{

namespace
{
namespace sp = uv::tok::space;

constexpr int rowH = (int) uv::tok::size::controlH;
constexpr int rowGap = (int) sp::space4;
/* The Bus select: its label as the web editor set it (labelWidth 28), and the
 * field just wide enough for "16" and the chevron. */
constexpr int busLabelWidth = 28;
constexpr int busFieldWidth = 64;

/*
 * The buses as the parameter prints them, one per step. Named here rather
 * than left to ParamSelect because a JUCE integer parameter is not discrete
 * (AudioProcessorParameter::isDiscrete), so getAllValueStrings() is empty for
 * it; the text is still the parameter's own, from the plugin.
 */
juce::StringArray busOptions (juce::RangedAudioParameter& p)
{
    juce::StringArray out;
    for (int i = 0; i < numBuses; ++i)
        out.add (p.getText ((float) i / (float) (numBuses - 1), 0));
    return out;
}
} // namespace

float meterFraction (float peak)
{
    if (! (peak > 0.0f))
        return 0.0f;
    const float db = 20.0f * std::log10 (peak);
    if (db <= meterFloorDb)
        return 0.0f;
    if (db >= 0.0f)
        return 1.0f;
    return 1.0f - db / meterFloorDb;
}

ni::ui::Led::Status ledStatus (Status s)
{
    switch (s)
    {
        case Status::live:        return ni::ui::Led::Status::on;
        case Status::taken:       return ni::ui::Led::Status::warn;
        case Status::unavailable: return ni::ui::Led::Status::clip;
        case Status::idle:        break;
    }
    return ni::ui::Led::Status::off;
}

/* ============================================================= content == */

ListenInEditor::Content::Content (Model& m)
    : bus (m.parameter (param::bus), busOptions (m.parameter (param::bus)))
{
    bus.setLabel ("Bus", busLabelWidth);
    bus.setFieldWidth (busFieldWidth);
    ni::ui::setInfo (bus, info::bus);

    name.setPlaceholder ("name this bus");
    name.setMaxLength (maxLabelBytes);
    name.setTitle ("Bus name");
    ni::ui::setInfo (name, info::name);

    level.setTitle ("Input level");
    /* A display, but one with a line: the pointer finds it (Meter.h takes
     * none of its own). */
    level.setInterceptsMouseClicks (true, false);
    ni::ui::setInfo (level, info::level);

    /* The LED's cell is as wide as its longest word, so the meter beside it
     * does not move when the status changes. */
    for (auto s : { Status::idle, Status::live, Status::taken, Status::unavailable })
        statusWidth = juce::jmax (statusWidth, ni::ui::Led::idealWidthFor (info::statusText (s)));

    /* Tab goes Bus, then Name, then the bar's Motion switch. */
    bus.setExplicitFocusOrder (1);
    name.setExplicitFocusOrder (2);

    for (auto* c : std::initializer_list<juce::Component*> { &bus, &name, &status, &level })
        addAndMakeVisible (c);
}

void ListenInEditor::Content::paint (juce::Graphics& g)
{
    ni::ui::paintChildLights (g, *this);
}

void ListenInEditor::Content::paintLight (juce::Graphics& g)
{
    ni::ui::forwardChildLights (g, *this);
}

void ListenInEditor::Content::resized()
{
    auto area = getLocalBounds();

    auto top = area.removeFromTop (rowH);
    bus.setBounds (top.removeFromLeft (bus.idealWidth()));
    top.removeFromLeft ((int) sp::space4);
    name.setBounds (top);

    area.removeFromTop (rowGap);
    auto second = area.removeFromTop (rowH);
    status.setBounds (second.removeFromLeft (statusWidth));
    second.removeFromLeft ((int) sp::space4);
    level.setBounds (second.withSizeKeepingCentre (second.getWidth(), ni::ui::Meter::height));
}

/* ============================================================== editor == */

ListenInEditor::ListenInEditor (Model& m, ni::ui::EditorFrame::Clock clock)
    : model (m),
      window (m, std::move (clock)),
      content (m),
      fit (window, designWidth, designHeight)
{
    window.setConventions ({ { info::pickVerb, info::pickRest } });
    window.setMotionInfo (info::motion);
    window.setSignatureInfo (info::signature);
    window.setSize (designWidth, designHeight);
    window.setContent (&content);

    content.name.onCommit = [this] (const juce::String& typed) { nameTyped (typed); };

    addAndMakeVisible (fit);
    setSize (designWidth, designHeight);

    refresh();
    frames = window.frameClock().subscribe ([this] (double) { refresh(); });
}

ListenInEditor::~ListenInEditor()
{
    frames.reset();
    window.setContent (nullptr);
}

void ListenInEditor::nameTyped (const juce::String& typed)
{
    model.setLabel (typed);
    /* What the plugin kept, which may be less than was typed: a colon or a
     * control character dropped, a long name cut on a character boundary. */
    content.name.setValue (model.label());
    refresh();
}

void ListenInEditor::refresh()
{
    const auto s = model.status();
    const auto label = model.label();

    content.status.setStatus (ledStatus (s));
    content.status.setLabel (info::statusText (s));

    const auto line = info::statusLine (s, model.parameter (param::bus).getCurrentValueAsText(), label);
    if (line != ni::ui::infoOf (content.status))
        ni::ui::setInfo (content.status, line);

    content.level.setLevel (meterFraction (model.peak()));
    content.level.setLive (s == Status::live);

    /* A name set elsewhere -- a state load, another editor -- shows at once;
     * one being typed is the field's until the edit ends (TextField.h). */
    if (label != content.name.getValue())
        content.name.setValue (label);
}

void ListenInEditor::constrain (juce::ComponentBoundsConstrainer& c)
{
    ni::ui::constrainToDesign (c, designWidth, designHeight);
}

void ListenInEditor::resized()
{
    fit.setBounds (getLocalBounds());
}

} // namespace ni::li
