// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Vocalize's window. Editor.h has its layout and its rules.
 */
#include "Editor.h"

#include "InfoLines.h"

#include "ChildLights.h"
#include "Info.h"
#include "Luminous.h"
#include "UvTokens.h"

namespace ni::vz
{

namespace
{
constexpr int knobGap = (int) uv::tok::space::space2;
constexpr int rowGap = (int) uv::tok::space::space3;
constexpr int rowH = (int) uv::tok::size::controlH;

/* What each panel holds, by parameter: its knobs, and Mode in Vocal's. */
struct Group
{
    const char* title;
    ni::ui::InfoText line;
    std::vector<int> knobs;
};

const std::array<Group, 3>& groups()
{
    static const std::array<Group, 3> g { {
        { "Vowel", info::vowelPanel, { param::vowel, param::size, param::resonance } },
        { "Vocal", info::vocalPanel, { param::vocal, param::excite } },
        { "Level", info::levelPanel, { param::mix, param::output } },
    } };
    return g;
}

struct Labels
{
    const char* label;
    ni::ui::InfoText line;
    ni::ui::InfoText value;
};

Labels labelsOf (int index)
{
    switch (index)
    {
        case param::vowel:     return { "Vowel", info::vowel, info::vowelValue };
        case param::size:      return { "Size", info::size, info::percentValue };
        case param::resonance: return { "Resonance", info::resonance, info::percentValue };
        case param::vocal:     return { "Vocal", info::vocal, info::percentValue };
        case param::excite:    return { "Excite", info::excite, info::percentValue };
        case param::mix:       return { "Mix", info::mix, info::percentValue };
        default:               return { "Output", info::output, info::dbValue };
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
    View() { setTitle ("NI Vocalize"); }

    void paint (juce::Graphics& g) override { ni::ui::paintChildLights (g, *this); }
    void paintLight (juce::Graphics& g) override { ni::ui::forwardChildLights (g, *this); }
};

/* ------------------------------------------------------------- editor -- */

Editor::Editor (Model& m, ni::ui::EditorFrame::Clock clock)
    : model (m),
      window (m, std::move (clock)),
      view (std::make_unique<View>()),
      chart (std::make_unique<VowelChart> (m)),
      envelope (std::make_unique<EnvelopePlot> (m))
{
    view->addAndMakeVisible (*chart);
    view->addAndMakeVisible (*envelope);

    for (size_t p = 0; p < groups().size(); ++p)
    {
        const auto& group = groups()[p];
        panels[p] = std::make_unique<ni::ui::Panel> (group.title);
        panels[p]->setTitleInfo (group.line);
        view->addAndMakeVisible (*panels[p]);

        for (const int index : group.knobs)
        {
            const auto words = labelsOf (index);
            auto k = std::make_unique<ni::ui::ParamKnob> (model.parameter (index), words.label);
            k->setBipolar (model.bipolar (index));
            ni::ui::setInfo (*k, words.line);
            k->setReadoutInfo (words.value);
            panels[p]->addAndMakeVisible (*k);
            knobs.push_back (std::move (k));
        }
    }
    /* The five vowels are where a drag holds: 0 to 4 in quarters. */
    knob (param::vowel).setDetents ({ 0.0, 0.25, 0.5, 0.75, 1.0 });

    mode = std::make_unique<ni::ui::ParamSelect> (model.parameter (param::mode));
    mode->setLabel ("Mode", 44);
    mode->setFieldWidth (112);   // "Spectral" whole
    ni::ui::setInfo (*mode, info::mode);
    panels[1]->addAndMakeVisible (*mode);

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

void Editor::layout()
{
    const int x1 = column + gutter;
    chart->setBounds (0, 0, column, plotH);
    envelope->setBounds (x1, 0, 2 * column + gutter, plotH);

    for (size_t p = 0; p < panels.size(); ++p)
    {
        auto& panel = *panels[p];
        panel.setBounds ((int) p * x1, panelsY, column, panelH);
        const auto area = panel.contentBounds();

        /* Three cells a row in every panel, so the dials line up across the
         * window; a panel of two leaves its third empty. */
        const int cell = (area.getWidth() - 2 * knobGap) / 3;
        const auto& group = groups()[p];
        for (size_t i = 0; i < group.knobs.size(); ++i)
            knob (group.knobs[i]).setBounds (area.getX() + (int) i * (cell + knobGap), area.getY(), cell,
                                             ni::ui::Knob::cardHeight);
    }

    const auto vocal = panels[1]->contentBounds();
    mode->setBounds (vocal.getX(), vocal.getY() + ni::ui::Knob::cardHeight + rowGap, mode->idealWidth(), rowH);
}

void Editor::tick (double)
{
    chart->update();
    envelope->update();
}

void Editor::resized()
{
    scaler.setBounds (getLocalBounds());
    layout();
}

} // namespace ni::vz
