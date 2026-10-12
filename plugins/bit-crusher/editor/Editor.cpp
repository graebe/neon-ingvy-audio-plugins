// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bit-Crusher's window. Editor.h has its layout and its rules.
 */
#include "Editor.h"

#include "InfoLines.h"

#include "ChildLights.h"
#include "Info.h"
#include "Luminous.h"
#include "ParamChoiceKnob.h"
#include "UvTokens.h"

namespace ni::bc
{

namespace
{
constexpr int knobGap = (int) uv::tok::space::space2;
constexpr int rowGap = (int) uv::tok::space::space3;
constexpr int toggleGap = (int) uv::tok::space::space4;
constexpr int rowH = (int) uv::tok::size::controlH;
/* Refresh's words are short: "Live", "10/s", "4/s", "1/s". */
constexpr int refreshLabelW = 56;
constexpr int refreshFieldW = 72;

/* What each panel holds, by parameter: three knobs, then its switches. */
struct Group
{
    const char* title;
    ni::ui::InfoText line;
    std::array<int, 3> knobs;
    std::vector<int> toggles;
};

const std::array<Group, 4>& groups()
{
    static const std::array<Group, 4> g { {
        { "Amplitude Crush", info::amplitudePanel, { param::bits, param::shape, param::dither }, { param::midRise, param::antiAlias } },
        { "Time Crush", info::timePanel, { param::rate, param::jitter, param::postOctave }, { param::preFilter, param::postFilter } },
        { "Level", info::levelPanel, { param::drive, param::mix, param::output }, {} },
        { "Filter", info::filterPanel, { param::cutoff, param::peak, param::slope }, { param::filter } },
    } };
    return g;
}

struct Labels
{
    const char* label;
    ni::ui::InfoText line;
    ni::ui::InfoText value;
};

/* A knob's label and lines, or a switch's label and line. */
Labels labelsOf (int index)
{
    switch (index)
    {
        case param::bits:       return { "Bits", info::bits, info::bitsValue };
        case param::shape:      return { "Shape", info::shape, info::percentValue };
        case param::dither:     return { "Dither", info::dither, info::percentValue };
        case param::rate:       return { "Rate", info::rate, info::rateValue };
        case param::jitter:     return { "Jitter", info::jitter, info::percentValue };
        case param::postOctave: return { "Octave", info::octave, info::octaveValue };
        case param::drive:      return { "Drive", info::drive, info::dbValue };
        case param::mix:        return { "Mix", info::mix, info::percentValue };
        case param::output:     return { "Output", info::output, info::dbValue };
        case param::midRise:    return { "Mid-Rise", info::midRise, info::midRise };
        case param::antiAlias:  return { "Anti-Alias", info::antiAlias, info::antiAlias };
        case param::preFilter:  return { "Pre", info::preFilter, info::preFilter };
        case param::cutoff:     return { "Cutoff", info::cutoff, info::cutoffValue };
        case param::peak:       return { "Peak", info::peak, info::dbValue };
        case param::slope:      return { "Slope", info::slope, info::slopeValue };
        case param::filter:     return { "Filter", info::filter, info::filter };
        default:                return { "Post", info::postFilter, info::postFilter };
    }
}
} // namespace

/* --------------------------------------------------------------- view -- */

/* The content: lets its controls' light -- a knob's focus ring at a panel's
 * edge -- reach past it. */
class Editor::View final : public juce::Component,
                           public ni::ui::Luminous
{
public:
    View() { setTitle ("NI Bit-Crusher"); }

    void paint (juce::Graphics& g) override { ni::ui::paintChildLights (g, *this); }
    void paintLight (juce::Graphics& g) override { ni::ui::forwardChildLights (g, *this); }
};

/* ------------------------------------------------------------- editor -- */

Editor::Editor (Model& m, ni::ui::EditorFrame::Clock clock)
    : model (m),
      window (m, std::move (clock)),
      view (std::make_unique<View>()),
      amplitude (std::make_unique<AmplitudePlot> (m)),
      timeCrush (std::make_unique<TimePlot> (m)),
      signal (std::make_unique<SignalPlot>()),
      filterResponse (std::make_unique<FilterPlot> (m))
{
    view->addAndMakeVisible (*amplitude);
    view->addAndMakeVisible (*timeCrush);
    view->addAndMakeVisible (*signal);
    view->addAndMakeVisible (*filterResponse);

    for (size_t p = 0; p < groups().size(); ++p)
    {
        const auto& group = groups()[p];
        panels[p] = std::make_unique<ni::ui::Panel> (group.title);
        panels[p]->setTitleInfo (group.line);
        view->addAndMakeVisible (*panels[p]);

        for (const int index : group.knobs)
        {
            const auto words = labelsOf (index);
            /* A choice -- Slope -- turns option by option. */
            auto& parameter = model.parameter (index);
            std::unique_ptr<ni::ui::ParamKnob> k;
            if (dynamic_cast<juce::AudioParameterChoice*> (&parameter) != nullptr)
                k = std::make_unique<ni::ui::ParamChoiceKnob> (parameter, words.label);
            else
                k = std::make_unique<ni::ui::ParamKnob> (parameter, words.label);
            k->setBipolar (model.bipolar (index));
            ni::ui::setInfo (*k, words.line);
            k->setReadoutInfo (words.value);
            panels[p]->addAndMakeVisible (*k);
            knobs.push_back (std::move (k));
        }
        for (const int index : group.toggles)
        {
            const auto words = labelsOf (index);
            auto t = std::make_unique<ni::ui::ParamToggle> (model.parameter (index), words.label);
            ni::ui::setInfo (*t, words.line);
            panels[p]->addAndMakeVisible (*t);
            toggles.push_back (std::move (t));
        }
    }

    /* WHAT THE PICTURES SHOW, AND HOW OFTEN: in the Level panel's free row. */
    refresh = std::make_unique<ni::ui::ParamSelect> (model.parameter (param::refresh));
    refresh->setLabel ("Refresh", refreshLabelW);
    refresh->setFieldWidth (refreshFieldW);
    ni::ui::setInfo (*refresh, info::refresh);
    panels[2]->addAndMakeVisible (*refresh);
    pause.setTitle (info::pauseTitle);
    ni::ui::setInfo (pause, info::pause);
    pause.setOn (false);
    pause.onClick = [this] { setPaused (! paused); };
    panels[2]->addAndMakeVisible (pause);

    /* THE WINDOW. */
    window.setConventions ({ { "drag", "a knob to turn it" }, { "shift", "for fine" }, { "double-click", "to reset" } });
    window.setMotionInfo (info::motion);
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

ni::ui::ParamKnob& Editor::knob (int parameterIndex)
{
    for (auto& k : knobs)
        if (k->binding().parameter().getParameterIndex() == parameterIndex)
            return *k;
    jassertfalse;
    return *knobs.front();
}

ni::ui::ParamToggle& Editor::toggle (int parameterIndex)
{
    for (auto& t : toggles)
        if (t->binding().parameter().getParameterIndex() == parameterIndex)
            return *t;
    jassertfalse;
    return *toggles.front();
}

void Editor::layout()
{
    const int x1 = column + gutter;
    amplitude->setBounds (0, 0, column, plotH);
    timeCrush->setBounds (x1, 0, column, plotH);
    signal->setBounds (2 * x1, 0, column, plotH);
    filterResponse->setBounds (3 * x1, 0, column, plotH);

    for (size_t p = 0; p < panels.size(); ++p)
    {
        auto& panel = *panels[p];
        panel.setBounds ((int) p * x1, panelsY, column, panelH);
        const auto area = panel.contentBounds();

        /* Three knobs share the row, the outer two flush with its edges. */
        const auto& group = groups()[p];
        const int cell = (area.getWidth() - 2 * knobGap) / 3;
        for (size_t i = 0; i < group.knobs.size(); ++i)
            knob (group.knobs[i]).setBounds (area.getX() + (int) i * (cell + knobGap), area.getY(), cell,
                                             ni::ui::Knob::cardHeight);

        /* The switches under them, left to right at their own width. */
        int x = area.getX();
        const int y = area.getY() + ni::ui::Knob::cardHeight + rowGap;
        for (const int index : group.toggles)
        {
            auto& t = toggle (index);
            t.setBounds (x, y, t.idealWidth(), rowH);
            x += t.idealWidth() + toggleGap;
        }
    }

    /* Refresh from the left of the Level panel's row, Pause at its right. */
    const auto level = panels[2]->contentBounds();
    const int y = level.getY() + ni::ui::Knob::cardHeight + rowGap;
    refresh->setBounds (level.getX(), y, refresh->idealWidth(), rowH);
    pause.setBounds (level.getRight() - rowH, y, rowH, rowH);
}

void Editor::tick (double nowMs)
{
    /* A new look at Refresh's pace -- Live takes every one -- and none while
     * paused; all three wells get the same one. A look made with other
     * settings than the knobs' is replaced the moment a current one exists. */
    bool fresh = false;
    if (! paused)
    {
        const auto& latest = model.scope();
        const bool due = nowMs - lookedAt >= model.refreshPeriodMs();
        const bool catchUp = ! current (look) && current (latest);
        if (latest.count() > 0 && latest.serial != look.serial && (due || catchUp))
        {
            look = latest;
            lookedAt = nowMs;
            fresh = true;
        }
    }
    const bool live = current (look);
    amplitude->update (look, fresh, live);
    timeCrush->update (look, fresh, live, nowMs);
    signal->update (look, fresh, nowMs);
    filterResponse->update();
}

bool Editor::current (const Scope& candidate) const
{
    if (candidate.count() == 0 || candidate.settled < Scope::settledWindow)
        return false;
    for (const int i : { param::drive, param::bits, param::shape, param::midRise, param::dither,
                         param::antiAlias, param::rate, param::jitter, param::preFilter })
        if (! juce::exactlyEqual (candidate.settings[(size_t) i], model.setting (i)))
            return false;
    return true;
}

void Editor::setPaused (bool on)
{
    paused = on;
    pause.setOn (on);
    pause.setTitle (on ? info::resumeTitle : info::pauseTitle);
    ni::ui::setInfo (pause, on ? info::resume : info::pause);
    /* Let go, the next look is taken at once. */
    if (! on)
        lookedAt = -1.0e300;
}

void Editor::resized()
{
    scaler.setBounds (getLocalBounds());
    layout();
}

} // namespace ni::bc
