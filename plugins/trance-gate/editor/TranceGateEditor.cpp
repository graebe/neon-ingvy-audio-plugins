// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate's editor. TranceGateEditor.h has the layout and the rules.
 */
#include "TranceGateEditor.h"

#include "InfoLines.h"
#include "Outcome.h"

#include "ChildLights.h"
#include "Info.h"
#include "UvTokens.h"

#include <algorithm>
#include <cmath>

namespace ni::tg
{

namespace
{
/* The gaps: a panel's knobs space-4 apart, the rows' controls space-4, the
 * Fade panel's stacked controls space-2. */
constexpr int cellGap = (int) uv::tok::space::space4;
constexpr int rowGap = (int) uv::tok::space::space4;
constexpr int stackGap = (int) uv::tok::space::space2;
constexpr int rowH = (int) uv::tok::size::controlH;

/* Length is 1..128 steps over its 127 intervals: the host's integer. */
float lengthNorm (int steps)
{
    return (float) (juce::jlimit (1, maxSteps, steps) - 1) / (float) (maxSteps - 1);
}
} // namespace

/* --------------------------------------------------------------- view -- */

/* The content: lays out nothing itself, and lets its controls' light -- a
 * pad's glow at the bottom edge, the ring's focus -- reach past it. */
class TranceGateEditor::View final : public juce::Component,
                                     public ni::ui::Luminous
{
public:
    View() { setTitle ("NI Trance Gate"); }

    void paint (juce::Graphics& g) override { ni::ui::paintChildLights (g, *this); }
    void paintLight (juce::Graphics& g) override { ni::ui::forwardChildLights (g, *this); }
};

/* ------------------------------------------------------------- editor -- */

int TranceGateEditor::designHeightFor (int length)
{
    return ni::ui::EditorFrame::heightFor (padsY + Pads::heightFor (length));
}

TranceGateEditor::TranceGateEditor (Model& m, Clipboard& clipboard, FilePanels& files,
                                    ni::ui::EditorFrame::Clock clock)
    : model (m),
      window (m, std::move (clock)),
      view (std::make_unique<View>()),
      envelope (std::make_unique<EnvelopePlot>()),
      slotVerbs (std::make_unique<Verbs> (m, clipboard, files, [this] (const std::string& s) { report (s); })),
      plotBand (std::make_unique<Band>()),
      padGrid (std::make_unique<Pads> (edits)),
      timeParam (m.parameter (param::timeMode), [this]
      {
          if (knobs.empty())
              return;   // not built yet: the first tick reads them
          for (const int i : { param::attack, param::decay, param::release })
              knob (i).refresh();
      }),
      scaler (window, designWidth, designHeightFor (m.pattern().length))
{
    /* THE RING: the pattern on the window's face, editable where it is
     * drawn, and to the keyboard its Length. */
    patternRing.setTitle ("Length");
    patternRing.setRange (1, maxSteps);
    patternRing.valueText = [] (int n) { return juce::String (n) + " steps"; };
    ni::ui::setInfo (patternRing, info::ring);
    patternRing.setCentreInfo (info::steps);
    patternRing.onPress = [this] (int i, bool shift) { return edits.press (i, shift); };
    patternRing.onSweep = [this] (int i) { edits.sweep (i); };
    patternRing.onCount = [this] (int n) { knob (param::length).binding().commit (lengthNorm (n)); };
    view->addAndMakeVisible (patternRing);
    view->addAndMakeVisible (*envelope);
    view->addAndMakeVisible (slotVerbs->group());

    /* THE PANELS, each a function's controls. */
    const auto addPanel = [this] (const char* title, const juce::String& line)
    {
        auto p = std::make_unique<ni::ui::Panel> (title, ni::ui::Panel::Form::compact);
        p->setTitleInfo (line);
        view->addAndMakeVisible (*p);
        panels.push_back (std::move (p));
        return panels.back().get();
    };
    auto* gate = addPanel ("Gate", info::gatePanel);
    auto* shape = addPanel ("Envelope", info::envelopePanel);
    auto* fade = addPanel ("Fade", info::fadePanel);

    const auto addKnob = [this] (ni::ui::Panel& into, std::unique_ptr<ni::ui::ParamKnob> k,
                                 const juce::String& line, const juce::String& readoutLine)
    {
        ni::ui::setInfo (*k, line);
        k->setReadoutInfo (readoutLine);
        into.addAndMakeVisible (*k);
        knobs.push_back (std::move (k));
    };
    const auto knobFor = [this] (int index, const char* label)
    {
        return std::make_unique<ni::ui::ParamKnob> (model.parameter (index), label);
    };
    /* Rate and Length step: an arrow is one division, one step. */
    addKnob (*gate, std::make_unique<ni::ui::ParamChoiceKnob> (model.parameter (param::rate), "Rate"),
             info::rate, info::readout::rate);
    addKnob (*gate, std::make_unique<ni::ui::ParamChoiceKnob> (model.parameter (param::length), "Length"),
             info::length, info::readout::length);
    addKnob (*gate, knobFor (param::amount, "Amount"), info::amount, info::readout::amount);
    addKnob (*gate, knobFor (param::width, "Width"), info::width, info::readout::width);
    addKnob (*shape, knobFor (param::attack, "Attack"), info::attack, info::readout::attack);
    addKnob (*shape, knobFor (param::decay, "Decay"), info::decay, info::readout::decay);
    addKnob (*shape, knobFor (param::sustain, "Sustain"), info::sustain, info::readout::sustain);
    addKnob (*shape, knobFor (param::release, "Release"), info::release, info::readout::release);
    addKnob (*fade, knobFor (param::fade, "Fade"), info::fade, info::readout::fade);

    /* THE STAGES READ IN THE UNIT Env Time ASKS FOR, and take either: the
     * plugin's text and its reading of what is typed, never the editor's. */
    for (const int index : { param::attack, param::decay, param::release })
        knob (index).setText ([this, index] { return model.stageText (index); },
                              [this, index] (const juce::String& typed) { return model.stageValue (index, typed); });

    /* THE FADE PANEL'S SECOND COLUMNS: two switches, two actions. */
    outSwitch = std::make_unique<ni::ui::ParamToggle> (model.parameter (param::fadeDir), "Out");
    ni::ui::setInfo (*outSwitch, info::out);
    softSwitch = std::make_unique<ni::ui::ParamToggle> (model.parameter (param::fadeSoft), "Soft");
    ni::ui::setInfo (*softSwitch, info::soft);
    ni::ui::setInfo (order, info::order);
    ni::ui::setInfo (shuffle, info::shuffle);
    order.onClick = [this]
    {
        edits.setOrdering (! edits.ordering());
        /* The mode's own conventions take the bar: an outcome from before
         * it would sit over them. */
        if (edits.ordering())
            window.clearOutcome();
        refreshOrder();
        tick (window.frameClock().now());
    };
    shuffle.onClick = [this] { edits.shuffle(); };
    for (auto* c : std::initializer_list<juce::Component*> { outSwitch.get(), softSwitch.get(), &order, &shuffle })
        fade->addAndMakeVisible (*c);

    /* THE SETTINGS ROW: how the rest is read. Slot needs no label: it sits
     * above-left of the pads, where the StepGrid card puts a pattern select. */
    slotSelect = std::make_unique<ni::ui::ParamSelect> (model.parameter (param::slot));
    slotSelect->setTitle ("Slot");
    /* The web row's widths: 80 for a single digit, as SettingsRow.jsx. */
    slotSelect->setFieldWidth (80);
    ni::ui::setInfo (*slotSelect, info::slot);
    joinSwitch = std::make_unique<ni::ui::ParamToggle> (model.parameter (param::legato), "Join Neighbors");
    ni::ui::setInfo (*joinSwitch, info::join);
    curveSelect = std::make_unique<ni::ui::ParamSelect> (model.parameter (param::curve));
    curveSelect->setLabel ("Curve", 44);
    /* "Exponential" whole, in the system's face: 86px of text after the 12px
     * pad and the 28px caret room and the hairlines, so 128 on the 4px grid
     * (the proposed artboard's figure). The web row's 124 cut it to
     * "Exponent...". */
    curveSelect->setFieldWidth (128);
    ni::ui::setInfo (*curveSelect, info::curve);
    timeSwitch = std::make_unique<ni::ui::ParamToggle> (model.parameter (param::timeMode), "Time in %");
    ni::ui::setInfo (*timeSwitch, info::time);
    for (auto* c : std::initializer_list<juce::Component*> { slotSelect.get(), joinSwitch.get(), curveSelect.get(),
                                                             timeSwitch.get() })
        view->addAndMakeVisible (*c);

    view->addAndMakeVisible (*plotBand);
    view->addAndMakeVisible (*padGrid);

    /* THE WINDOW. */
    window.setConventions (info::conventions());
    window.setSignatureInfo (info::signature);
    window.setContent (view.get());
    addAndMakeVisible (scaler);

    rows = ni::ui::StepGrid::rowsFor (model.pattern().length);
    window.setSize (designWidth, scaler.getDesignHeight());
    layout();
    refreshOrder();
    tick (window.frameClock().now());

    frames = window.frameClock().subscribe ([this] (double now) { tick (now); });
    setSize (designWidth, scaler.getDesignHeight());
}

TranceGateEditor::~TranceGateEditor()
{
    frames.reset();
}

juce::Component& TranceGateEditor::content() noexcept { return *view; }

ni::ui::ParamKnob& TranceGateEditor::knob (int parameterIndex)
{
    for (auto& k : knobs)
        if (k->binding().parameter().getParameterIndex() == parameterIndex)
            return *k;
    jassertfalse;
    return *knobs.front();
}

ni::ui::ParamSelect& TranceGateEditor::select (int parameterIndex)
{
    return parameterIndex == param::curve ? *curveSelect : *slotSelect;
}

ni::ui::ParamToggle& TranceGateEditor::toggle (int parameterIndex)
{
    switch (parameterIndex)
    {
        case param::fadeDir:  return *outSwitch;
        case param::fadeSoft: return *softSwitch;
        case param::legato:   return *joinSwitch;
        default:              return *timeSwitch;
    }
}

float TranceGateEditor::plain (int parameterIndex)
{
    auto& p = model.parameter (parameterIndex);
    return p.convertFrom0to1 (p.getValue());
}

void TranceGateEditor::report (const std::string& sentence)
{
    window.showOutcome (outcome::clause (sentence));
}

/* ------------------------------------------------------------- layout -- */

void TranceGateEditor::layout()
{
    patternRing.setBounds (0, 0, ringSize, ringSize);
    envelope->setBounds (0, envelopeY, ringSize, envelopeH);
    /* The verbs in the side column, flush with the panels' top edge. */
    slotVerbs->group().setTopLeftPosition (sideX, 0);

    for (std::size_t i = 0; i < panels.size(); ++i)
        panels[i]->setBounds (panelX, (int) i * (ni::ui::Panel::compactHeight + panelGap), panelW,
                              ni::ui::Panel::compactHeight);
    layoutPanels();
    layoutRow();

    plotBand->setBounds (0, bandY, contentWidth, Band::height);
    padGrid->setBounds (0, padsY, contentWidth, Pads::heightFor (model.pattern().length));
}

/*
 * FOUR CELLS A PANEL, each as wide as the others, as the web's flex cards
 * were; each edge rounded on its own, as a browser snaps a fractional box.
 * The Fade panel's knob takes the first cell, so every knob in the window is
 * the same size, and its switches and actions stand in two columns after it.
 */
void TranceGateEditor::layoutPanels()
{
    const auto content = panels.front()->contentBounds();
    const float cell = (float) (content.getWidth() - 3 * cellGap) / 4.0f;
    const auto cellBounds = [&] (int i)
    {
        const float left = (float) content.getX() + (float) i * (cell + (float) cellGap);
        const int l = juce::roundToInt (left);
        return juce::Rectangle<int> (l, content.getY(), juce::roundToInt (left + cell) - l, content.getHeight());
    };

    const int perPanel[][4] { { param::rate, param::length, param::amount, param::width },
                              { param::attack, param::decay, param::sustain, param::release } };
    for (const auto& row : perPanel)
        for (int i = 0; i < 4; ++i)
            knob (row[i]).setBounds (cellBounds (i).withHeight (ni::ui::Knob::cardHeight));
    knob (param::fade).setBounds (cellBounds (0).withHeight (ni::ui::Knob::cardHeight));

    /* Two rows of 28 with space-2 between, centred on the knob's height. */
    const int stackH = 2 * rowH + stackGap;
    const int y0 = content.getY() + (ni::ui::Knob::cardHeight - stackH) / 2;
    const int switchX = cellBounds (1).getX();
    const int switchW = std::max (outSwitch->idealWidth(), softSwitch->idealWidth());
    outSwitch->setBounds (switchX, y0, switchW, rowH);
    softSwitch->setBounds (switchX, y0 + rowH + stackGap, switchW, rowH);

    /* As wide as the longest the order button can say, so it never moves. */
    const ni::ui::Button longest { "Set order 128/128" };
    const int buttonW = std::max (longest.idealWidth(), shuffle.idealWidth());
    const int buttonX = switchX + switchW + cellGap;
    order.setBounds (buttonX, y0, buttonW, rowH);
    shuffle.setBounds (buttonX, y0 + rowH + stackGap, buttonW, rowH);
}

void TranceGateEditor::layoutRow()
{
    int x = 0;
    const auto place = [&x] (juce::Component& c, int w)
    {
        c.setBounds (x, settingsY, w, rowH);
        x += w + rowGap;
    };
    place (*slotSelect, slotSelect->idealWidth());
    place (*joinSwitch, joinSwitch->idealWidth());
    place (*curveSelect, curveSelect->idealWidth());
    place (*timeSwitch, timeSwitch->idealWidth());
}

void TranceGateEditor::fitRows (int length)
{
    const int now = ni::ui::StepGrid::rowsFor (length);
    if (now == rows)
        return;
    rows = now;
    const int height = designHeightFor (length);
    scaler.setDesignSize (designWidth, height);
    window.setSize (designWidth, height);
    padGrid->setBounds (0, padsY, contentWidth, Pads::heightFor (length));
    const auto size = scaler.boundsAt (scaler.getScale());
    setSize (size.getWidth(), size.getHeight());
}

void TranceGateEditor::resized()
{
    scaler.setBounds (getLocalBounds());
}

/* --------------------------------------------------------------- frame -- */

void TranceGateEditor::refreshOrder()
{
    const bool on = edits.ordering();
    order.setOn (on);
    const auto text = on ? "Set order " + juce::String (edits.namedCount()) + "/" + juce::String (edits.arrivals())
                         : juce::String ("Set order");
    /* Every frame asks; only a new count repaints. */
    if (text != order.getText())
        order.setText (text);

    const bool holes = edits.fadeOut();
    if (on != orderShown || (on && holes != orderHoles))
    {
        orderShown = on;
        orderHoles = holes;
        window.setConventions (on ? info::orderConventions (holes) : info::conventions());
    }
}

void TranceGateEditor::tick (double nowMs)
{
    const auto& p = model.pattern();
    const int length = juce::jlimit (1, maxSteps, p.length);
    fitRows (length);

    const auto transport = model.transport();
    const double phase = transport.playing && length > 0
                           ? std::fmod (std::fmod (transport.phase, (double) length) + length, (double) length)
                           : -1.0;
    const int playStep = phase >= 0.0 ? (int) std::floor (phase) % length : -1;
    const float fade = plain (param::fade);
    const float amount = plain (param::amount) / 100.0f;

    /* The Length detents at this Rate and the host's meter: the ring's
     * pages and the Length knob's holds. */
    auto now = model.lengthDetents();
    if (now != detents)
    {
        detents = std::move (now);
        std::vector<double> steps, normalised;
        for (const int d : detents)
        {
            steps.push_back ((double) d);
            normalised.push_back ((double) lengthNorm (d));
        }
        patternRing.setDetents (steps);
        knob (param::length).setDetents (normalised);
    }

    /* THE RING: what was drawn and what sounds, as the pads show them -- a
     * step the fade has not brought in is hollow, and a hole Fade Out has not
     * removed yet is filled to its level, so neither reads as a gap. */
    patternRing.setCount (length);
    for (int i = 0; i < length; ++i)
    {
        const auto at = (std::size_t) i;
        const bool sounds = p.levels[at] > 0.0f;
        const bool drawn = p.steps[at] != StepMode::off;
        ni::ui::Ring::StepState s;
        s.on = sounds && p.steps[at] == StepMode::on;
        s.tie = sounds && p.steps[at] == StepMode::tie;
        s.pending = ! sounds && drawn;
        s.filled = sounds && ! drawn;
        s.amount = juce::jlimit (0.0f, 1.0f, drawn ? p.depths[at] * p.levels[at] : p.levels[at]);
        patternRing.setStep (i, s);
    }
    patternRing.setCursor (p.cursor >= 0 && p.cursor < length ? p.cursor : -1);
    patternRing.setPlayhead (playStep, playStep >= 0);
    patternRing.setCentre (juce::String (length), "Steps");

    padGrid->show (p, playStep, fade < 99.9f);
    refreshOrder();

    /* THE PLOTS: the engine's renders, Amount laid under them. */
    EnvelopePlot::Setting setting;
    setting.msPerStep = transport.msPerStep;
    setting.width = plain (param::width) / 100.0;
    setting.attack = plain (param::attack);
    setting.decay = plain (param::decay);
    setting.release = plain (param::release);
    setting.amount = amount;
    envelope->update (model.envelope(), setting);

    const auto& gate = model.gate();
    plotBand->pattern().update (gate, length, amount);
    plotBand->pattern().setPlayhead (playStep);
    plotBand->signal().update (model.capture(), gate, length, amount, transport.playing, nowMs);

    /* The stages in ms follow the tempo and Width, which are not theirs. */
    for (const int i : { param::attack, param::decay, param::release })
        knob (i).refresh();
}

} // namespace ni::tg
