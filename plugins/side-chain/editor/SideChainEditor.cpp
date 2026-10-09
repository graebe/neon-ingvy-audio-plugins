// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Side-Chain's editor. SideChainEditor.h has the layout and the rules.
 */
#include "SideChainEditor.h"

#include "InfoLines.h"

#include "ChildLights.h"
#include "Info.h"
#include "UvTokens.h"
#include "UvType.h"

#include <algorithm>

namespace ni::sc
{

namespace
{
namespace c = uv::tok::colour;

/* The rows' gaps: the knobs' space-6, the selects' space-4 (app.css). */
constexpr int knobGap = (int) uv::tok::space::space6;
constexpr int rowGap = (int) uv::tok::space::space4;
constexpr int rowH = (int) uv::tok::size::controlH;

/* The header's text: the hint style, in capitals, tracked wider (0.08em). */
const uv::tok::TextStyle headerStyle { uv::tok::type::hint.size, uv::tok::type::hint.lineHeight,
                                       uv::tok::type::hint.weight, 0.08f };

/* The text a choice parameter gives one of its options. */
juce::String optionText (const ni::ui::ParamBinding& b, int index)
{
    const int last = b.steps() - 1;
    return b.textFor (last > 0 ? (float) juce::jlimit (0, last, index) / (float) last : 0.0f);
}
} // namespace

/* ------------------------------------------------------------- header -- */

/*
 * THE TRIGGER'S STATE -- not the plugin's name, which the host shows -- and,
 * pushed to the right so it cannot be read as part of it, the window's single
 * amber mark.
 */
class SideChainEditor::Header final : public juce::Component
{
public:
    Header()
    {
        setInterceptsMouseClicks (false, false);
        setTitle ("Trigger state");
    }

    void set (const juce::String& stateLine, const juce::String& warningLine)
    {
        if (stateLine == state && warningLine == warning)
            return;
        state = stateLine;
        warning = warningLine;
        setDescription (warning.isEmpty() ? state : state + ", " + warning);
        repaint();
    }

    const juce::String& getState() const noexcept { return state; }
    const juce::String& getWarning() const noexcept { return warning; }

    void paint (juce::Graphics& g) override
    {
        const auto font = uv::type::font (headerStyle);
        const auto box = getLocalBounds().toFloat();
        const auto warn = warning.toUpperCase();
        const float warnW = warn.isEmpty() ? 0.0f : uv::type::width (font, warn);
        if (warnW > 0.0f)
            uv::type::draw (g, warn, box.withLeft (box.getRight() - warnW), font, c::amber,
                            juce::Justification::centredRight);
        uv::type::draw (g, state.toUpperCase(),
                        box.withRight (box.getRight() - (warnW > 0.0f ? warnW + uv::tok::space::space3 : 0.0f)),
                        font, c::inkMuted, juce::Justification::centredLeft);
    }

private:
    juce::String state, warning;
};

/* --------------------------------------------------------------- view -- */

/* The content: lays out nothing itself, and lets its controls' light -- a
 * knob's focus ring at the content's left edge -- reach past it. */
class SideChainEditor::View final : public juce::Component,
                                    public ni::ui::Luminous
{
public:
    View() { setTitle ("NI Side-Chain"); }

    void paint (juce::Graphics& g) override { ni::ui::paintChildLights (g, *this); }
    void paintLight (juce::Graphics& g) override { ni::ui::forwardChildLights (g, *this); }
};

/* ------------------------------------------------------------- editor -- */

SideChainEditor::SideChainEditor (Model& m, ni::ui::EditorFrame::Clock clock)
    : model (m),
      window (m, std::move (clock)),
      view (std::make_unique<View>()),
      header (std::make_unique<Header>()),
      plot (std::make_unique<Shaper> (m)),
      sourceParam (m.parameter (param::source), [this] { showSource(); }),
      timeParam (m.parameter (param::timeMode), [this] { refreshStages(); })
{
    view->addAndMakeVisible (*header);
    view->addAndMakeVisible (*plot);

    /* THE KNOBS: the shape's five, then the sources' own. */
    const auto addKnob = [this] (std::unique_ptr<ni::ui::ParamKnob> k, const juce::String& line,
                                 const juce::String& readoutLine)
    {
        ni::ui::setInfo (*k, line);
        k->setReadoutInfo (readoutLine);
        view->addChildComponent (*k);
        knobs.push_back (std::move (k));
    };
    const auto knobFor = [this] (int index, const char* label)
    {
        return std::make_unique<ni::ui::ParamKnob> (model.parameter (index), label);
    };
    addKnob (knobFor (param::depth, "Depth"), info::depth, info::depthValue);
    addKnob (knobFor (param::delay, "Delay"), info::delay, info::delayValue);
    addKnob (knobFor (param::attack, "Attack"), info::attack, info::attackValue);
    addKnob (knobFor (param::hold, "Hold"), info::hold, info::holdValue);
    addKnob (knobFor (param::release, "Release"), info::release, info::releaseValue);
    addKnob (std::make_unique<ni::ui::ParamChoiceKnob> (model.parameter (param::note), "Note"),
             info::note, info::noteValue);
    addKnob (std::make_unique<ni::ui::ParamChoiceKnob> (model.parameter (param::channel), "Channel"),
             info::channel, info::channelValue);
    addKnob (knobFor (param::velSens, "Velocity"), info::velSens, info::velSensValue);
    addKnob (knobFor (param::threshold, "Threshold"), info::threshold, info::thresholdValue);
    addKnob (knobFor (param::lockout, "Lockout"), info::lockout, info::lockoutValue);

    /* Delay runs both ways about no wait at all (Knob card, bipolar). */
    knob (param::delay).setBipolar (true);

    /* THE STAGES READ IN THE UNIT TIME ASKS FOR, and take either -- on the
     * knobs and on the handles alike, so a stage says and takes one thing
     * wherever it is reached. */
    for (const int index : { param::delay, param::attack, param::hold, param::release })
        knob (index).setText ([this, index] { return stageReadout (index); },
                              [this, index] (const juce::String& typed) { return stageTyped (index, typed); });
    plot->setStageText ([this] (int index) { return stageReadout (index); },
                        [this] (int index, const juce::String& typed) { return stageTyped (index, typed); });

    /* THE ROWS: the trigger's, then the shape's. */
    sourceSelect = std::make_unique<ni::ui::ParamSelect> (model.parameter (param::source));
    /* SOURCE is 46px in the label style; the proposed artboard's 44 would
     * run it into the field. */
    sourceSelect->setLabel ("Source", 52);
    sourceSelect->setFieldWidth (128);   // the canvas's: "Sidechain" whole
    ni::ui::setInfo (*sourceSelect, info::source);

    rateSelect = std::make_unique<ni::ui::ParamSelect> (model.parameter (param::rate));
    rateSelect->setLabel ("Rate", 38);
    rateSelect->setFieldWidth (84);
    ni::ui::setInfo (*rateSelect, info::rate);

    gateSwitch = std::make_unique<ni::ui::ParamToggle> (model.parameter (param::midiMode), "Gate");
    ni::ui::setInfo (*gateSwitch, info::gate);

    curveSelect = std::make_unique<ni::ui::ParamSelect> (model.parameter (param::curve));
    curveSelect->setLabel ("Curve", 42);
    /* The canvas's width, which "Exponential" fits; the web editor's 118 cut
     * it in the system's own face. */
    curveSelect->setFieldWidth (136);
    ni::ui::setInfo (*curveSelect, info::curve);

    percentSwitch = std::make_unique<ni::ui::ParamToggle> (model.parameter (param::timeMode), "% of cycle");
    ni::ui::setInfo (*percentSwitch, info::percent);

    view->addAndMakeVisible (*sourceSelect);
    view->addChildComponent (*rateSelect);
    view->addChildComponent (*gateSwitch);
    view->addAndMakeVisible (*curveSelect);
    view->addAndMakeVisible (*percentSwitch);

    /* THE WINDOW. */
    window.setConventions (conventions());
    window.setMotionInfo (info::motion);
    window.setSignatureInfo (info::signature);
    window.setContent (view.get());
    addAndMakeVisible (scaler);

    window.setSize (designWidth, designHeight);
    showSource();
    tick (window.frameClock().now());

    frames = window.frameClock().subscribe ([this] (double now) { tick (now); });
    setSize (designWidth, designHeight);
}

SideChainEditor::~SideChainEditor()
{
    frames.reset();
}

juce::Component& SideChainEditor::content() noexcept { return *view; }

const juce::String& SideChainEditor::stateText() const noexcept { return header->getState(); }
const juce::String& SideChainEditor::warningText() const noexcept { return header->getWarning(); }

ni::ui::ParamKnob& SideChainEditor::knob (int parameterIndex)
{
    for (auto& k : knobs)
        if (k->binding().parameter().getParameterIndex() == parameterIndex)
            return *k;
    jassertfalse;
    return *knobs.front();
}

ni::ui::ParamSelect& SideChainEditor::select (int parameterIndex)
{
    switch (parameterIndex)
    {
        case param::rate:  return *rateSelect;
        case param::curve: return *curveSelect;
        default:           return *sourceSelect;
    }
}

ni::ui::ParamToggle& SideChainEditor::toggle (int parameterIndex)
{
    return parameterIndex == param::midiMode ? *gateSwitch : *percentSwitch;
}

/*
 * WHICH SOURCE'S CONTROLS SHOW follows the Source PARAMETER -- the control
 * you just changed -- so the window answers the choice at once. The source's
 * knobs follow the shape's five, and the row's width is shared out again among
 * all that show, as the web row's was: the knobs keep their order and only
 * their widths change, and the two rows under them do not move.
 */
void SideChainEditor::showSource()
{
    const int last = sourceParam.steps() - 1;
    const auto source = (Source) (last > 0 ? juce::roundToInt (sourceParam.value() * (float) last) : 0);
    const bool midi = source == Source::midi, key = source == Source::sidechain;

    knob (param::note).setVisible (midi);
    knob (param::channel).setVisible (midi);
    knob (param::velSens).setVisible (midi);
    knob (param::threshold).setVisible (key);
    knob (param::lockout).setVisible (key);
    for (const int shape : { param::depth, param::delay, param::attack, param::hold, param::release })
        knob (shape).setVisible (true);
    rateSelect->setVisible (source == Source::cycle);
    gateSwitch->setVisible (midi);
    layoutRows();
}

void SideChainEditor::layoutRows()
{
    /* The content's width is the design's, whatever has been laid out yet. */
    const int width = designWidth - 2 * ni::ui::EditorFrame::padding;
    header->setBounds (0, 0, width, headerH);
    plot->setBounds (0, plotY, width, plotH);

    /* THE KNOBS SHARE THE ROW, each as wide as the others and never narrower
     * than its readout: the web's knob cards were flex 1 in a flex row. Each
     * edge is rounded on its own, as a browser snaps a fractional box, so the
     * last card ends on the row's right edge. */
    const auto shown = (int) std::count_if (knobs.begin(), knobs.end(), [] (const auto& k) { return k->isVisible(); });
    const float cell = shown > 0 ? std::max ((float) ni::ui::Knob::minWidth,
                                             (float) (width - (shown - 1) * knobGap) / (float) shown)
                                 : 0.0f;
    int i = 0;
    for (auto& k : knobs)
        if (k->isVisible())
        {
            const float left = (float) i++ * (cell + (float) knobGap);
            const int l = juce::roundToInt (left);
            k->setBounds (l, knobsY, juce::roundToInt (left + cell) - l, ni::ui::Knob::cardHeight);
        }

    /* A row, left to right, of what is showing in it. */
    int x = 0;
    const auto place = [&x] (juce::Component& c, int w, int y)
    {
        if (! c.isVisible())
            return;
        c.setBounds (x, y, w, rowH);
        x += w + rowGap;
    };
    place (*sourceSelect, sourceSelect->idealWidth(), triggerRowY);
    place (*rateSelect, rateSelect->idealWidth(), triggerRowY);
    place (*gateSwitch, gateSwitch->idealWidth(), triggerRowY);

    x = 0;
    place (*curveSelect, curveSelect->idealWidth(), shapeRowY);
    place (*percentSwitch, percentSwitch->idealWidth(), shapeRowY);
}

bool SideChainEditor::stagesInMs() const
{
    return readsInMs (timeParam.value(), lastState.msPerCycle);
}

juce::String SideChainEditor::stageReadout (int index)
{
    const auto ms = [this, index]
    {
        switch (index)
        {
            case param::delay:  return lastStages.delay;
            case param::attack: return lastStages.attack;
            case param::hold:   return lastStages.hold;
            default:            return lastStages.release;
        }
    }();
    return stageText (ms, stagesInMs(), knob (index).binding().text());
}

std::optional<float> SideChainEditor::stageTyped (int index, const juce::String& typed)
{
    return parseStage (knob (index).binding().parameter(), typed, stagesInMs(), lastState.msPerCycle);
}

void SideChainEditor::refreshStages()
{
    for (const int index : { param::delay, param::attack, param::hold, param::release })
        knob (index).refresh();
}

void SideChainEditor::tick (double nowMs)
{
    plot->update (nowMs);

    lastState = model.state();
    lastStages = model.stageMs();
    refreshStages();

    watch.observe (lastState.fires, nowMs);
    const auto sourceName = optionText (sourceParam, (int) lastState.source);
    const auto& rate = rateSelect->binding();
    header->set (stateLine (sourceName, optionText (rate, lastState.rate), lastState.source, lastState.stage),
                 warningFor (lastState, model.buses(), ! plot->isQuiet(), watch.isQuiet (nowMs)));
}

void SideChainEditor::resized()
{
    scaler.setBounds (getLocalBounds());
    layoutRows();
}

} // namespace ni::sc
