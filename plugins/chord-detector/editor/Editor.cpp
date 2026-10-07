// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Chord-Detector's window. Editor.h has its layout and its rules.
 */
#include "Editor.h"

#include "ChildLights.h"
#include "UvGround.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

#include <cmath>

namespace ni::chord_detector
{

namespace
{
namespace c = uv::tok::colour;
using ni::ui::InfoText;

/* The window's lines: what each control does, at most 72 characters. */
constexpr InfoText keyLine { "Key — click a key on the circle, or step it a fifth with the arrows." };
constexpr InfoText modeLine { "Mode — the scale the key's notes and numerals are read in." };
constexpr InfoText spellingLine { "Spelling — name notes as the key writes them, or always # or b." };
constexpr InfoText holdLine { "Hold — keep the last chord named after the keys come up." };
constexpr InfoText viewLine { "History — notation on a grand staff, or one line per MIDI note." };
constexpr InfoText spanLine { "Span — how many bars of history the window shows." };
constexpr InfoText zoomLine { "Zoom — the window's size." };

/* Where things are, in design pixels. */
const juce::Rectangle<int> circleArea { 32, 32, 232, 232 };
const juce::Rectangle<int> readoutArea { 296, 32, 432, ni::ui::ChordReadout::idealHeight };
constexpr int controlsTop = 236;
constexpr int historyTop = 288;
const juce::Rectangle<int> historyWell { 32, 324, 696, 136 };
const juce::Rectangle<int> keyboardWell { 32, 476, 696, 72 };
const juce::Rectangle<int> keyboardArea { 44, 480, 672, 62 };
constexpr int hintTop = Editor::height - 16 - ni::ui::Hint::height;
constexpr int right = 728;
constexpr int rowH = (int) uv::tok::size::controlH;
constexpr int gap6 = (int) uv::tok::space::space6;
constexpr float lens = 8.0f;

/* A choice parameter's index from its normalised value. */
int indexOf (float normalised, int count)
{
    return count > 1 ? juce::jlimit (0, count - 1, (int) std::lround (normalised * (float) (count - 1))) : 0;
}

/* The pitch-class name of a written note: "Bb3" -> "Bb". */
juce::String pitchName (const CdWrittenNote& w)
{
    return juce::String::fromUTF8 (w.name).trimCharactersAtEnd ("-0123456789");
}

juce::String signatureText (int signature)
{
    if (signature == 0)
        return "no sharps or flats";
    const int n = std::abs (signature);
    return juce::String (n) + (signature > 0 ? " sharp" : " flat") + (n > 1 ? "s" : "");
}

ni::ui::music::NoteSet noteSet (const uint64_t (&words)[2])
{
    ni::ui::music::NoteSet s;
    for (int n = 0; n < 128; ++n)
        if ((words[n / 64] >> (n % 64)) & 1u)
            s.set ((size_t) n);
    return s;
}
} // namespace

Editor::Editor (Model& m) : model (m)
{
    setOpaque (true);
    setWantsKeyboardFocus (false);

    for (int i = 0; i < paramCount; ++i)
        bindings[(size_t) i] = std::make_unique<ni::ui::ParamBinding> (model.parameter (i), [this] { showParameters(); });

    /* The circle, and the mode in its centre. */
    ni::ui::setInfo (circle, keyLine);
    circle.onTonicSelected = [this] (int pc) { commit (Param::tonic, pc); };
    addAndMakeVisible (circle);
    bind (modeBox, Param::mode);
    modeBox.setTitle ("Mode");
    modeBox.setFieldWidth (104);
    ni::ui::setInfo (modeBox, modeLine);

    addAndMakeVisible (readout);

    /* Spelling, Hold. */
    const char* spellingLabels[] = { "Auto", "#", "b" };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = spellings[(size_t) i];
        b.setText (spellingLabels[i]);
        b.setSize (std::max (40, b.idealWidth()), rowH);
        b.onClick = [this, i] { commit (Param::spelling, i); };
        ni::ui::setInfo (b, spellingLine);
        spellingGroup.add (b);
    }
    spellingGroup.setTitle ("Spelling");
    addAndMakeVisible (spellingGroup);
    holdToggle.onChange = [this] (bool on) { commit (Param::hold, on ? 1 : 0); };
    ni::ui::setInfo (holdToggle, holdLine);
    addAndMakeVisible (holdToggle);

    /* The history's header, and its two views. */
    bind (zoomBox, Param::zoom);
    zoomBox.setLabel ("Zoom");
    zoomBox.setFieldWidth (88);
    ni::ui::setInfo (zoomBox, zoomLine);
    bind (spanBox, Param::historySpan);
    spanBox.setLabel ("Span");
    spanBox.setFieldWidth (104);
    ni::ui::setInfo (spanBox, spanLine);
    const char* viewLabels[] = { "Staff", "MIDI" };
    for (int i = 0; i < 2; ++i)
    {
        auto& b = views[(size_t) i];
        b.setText (viewLabels[i]);
        b.setSize (std::max (56, b.idealWidth()), rowH);
        b.onClick = [this, i] { commit (Param::historyView, i); };
        ni::ui::setInfo (b, viewLine);
        viewGroup.add (b);
    }
    viewGroup.setTitle ("History");
    addAndMakeVisible (viewGroup);

    staff.setWriter ([this] (int midi) {
        const auto w = model.write (midi);
        return ni::ui::GrandStaff::Written { w.staff_step, w.accidental };
    });
    roll.setNamer ([this] (int midi) { return juce::String::fromUTF8 (model.write (midi).name); });
    addChildComponent (staff);
    addChildComponent (roll);
    addAndMakeVisible (keyboard);

    /* The hint: conventions at rest, a control's line under the pointer. */
    bar.setConventions ({ { "play", "notes on this track" },
                          { "click", "a key on the circle" },
                          { "hold", "keeps the last chord" } });
    info.onChange = [this] { bar.setInfoLine (info.text()); };
    addAndMakeVisible (bar);

    /* Laid out once everything it lays out exists. */
    setSize (width, height);

    /* What the ring held before this window opened belongs to no history it
     * can show: the notes are taken from what sounds now instead. */
    CdNoteEvent stale[256];
    while (model.takeNotes (stale, (int) std::size (stale)) > 0)
    {
    }
    const auto& now = model.reading();
    seenDropped = now.dropped;
    history.setClock ({ now.now, now.bpm, now.bar, now.bar_origin, now.playing != 0 });
    reconcile (now);

    showParameters();
    tick();
    frame = clock.subscribe ([this] (double) { tick(); });
}

Editor::~Editor()
{
    frame.reset();
    info.onChange = nullptr;
}

int Editor::choiceOf (Param p) const
{
    const auto& b = *bindings[(size_t) p];
    return indexOf (b.value(), b.steps());
}

void Editor::commit (Param p, int choice)
{
    auto& b = binding (p);
    const int steps = std::max (2, b.steps());
    b.commit ((float) choice / (float) (steps - 1));
}

void Editor::bind (ni::ui::Select& select, Param p)
{
    select.setOptions (binding (p).choices());
    select.onChange = [this, p] (int index) { commit (p, index); };
    addAndMakeVisible (select);
}

void Editor::showParameters()
{
    modeBox.setIndex (choiceOf (Param::mode));
    spanBox.setIndex (choiceOf (Param::historySpan));
    zoomBox.setIndex (choiceOf (Param::zoom));
    for (int i = 0; i < 3; ++i)
        spellings[(size_t) i].setOn (choiceOf (Param::spelling) == i);
    holdToggle.setOn (choiceOf (Param::hold) == 1);
    const int view = choiceOf (Param::historyView);
    for (int i = 0; i < 2; ++i)
        views[(size_t) i].setOn (view == i);
    staff.setVisible (view == 0);
    roll.setVisible (view == 1);

    /* A new key changes the circle's names and tint and the staff's
     * signature at once: they are drawn from the parameters, not the reading. */
    shownSerial = 0xffffffffu;
    showReading (model.reading());
}

double Editor::spanQuarters (const CdReading& r) const
{
    static constexpr double bars[] = { 1.0, 2.0, 4.0, 8.0 };
    const double barLength = r.bar > 0.0 ? r.bar : 4.0;
    return bars[juce::jlimit (0, 3, choiceOf (Param::historySpan))] * barLength;
}

void Editor::reconcile (const CdReading& r)
{
    const auto sounding = noteSet (r.sounding);
    ni::ui::music::NoteSet known;
    for (const auto& n : history.notes())
        if (n.sounding())
            known.set ((size_t) n.midi);
    for (int m = 0; m < 128; ++m)
    {
        if (known.test ((size_t) m) && ! sounding.test ((size_t) m))
            history.stop (m, r.now);
        else if (sounding.test ((size_t) m) && ! known.test ((size_t) m))
            history.start (m, 100, r.now);
    }
}

void Editor::drainNotes()
{
    CdNoteEvent events[256];
    for (int n; (n = model.takeNotes (events, (int) std::size (events))) > 0;)
        for (int i = 0; i < n; ++i)
        {
            const auto& e = events[i];
            if (e.velocity > 0)
                history.start (e.note, e.velocity, e.at);
            else
                history.stop (e.note, e.at);
        }
}

void Editor::tick()
{
    /* The notes since the last frame, into the history. */
    drainNotes();

    const auto& r = model.reading();
    /* The longest span it can be asked to show, in this meter. */
    history.setKeep (8.0 * (r.bar > 0.0 ? r.bar : 4.0));
    history.setClock ({ r.now, r.bpm, r.bar, r.bar_origin, r.playing != 0 });
    if (r.dropped != seenDropped)
    {
        seenDropped = r.dropped;
        reconcile (r);
    }
    staff.setSpan (spanQuarters (r));
    roll.setSpan (spanQuarters (r));
    showReading (r);
    staff.repaint();
    roll.repaint();
}

void Editor::showReading (const CdReading& r)
{
    const bool keysMoved = noteSet (r.sounding) != keyboard.getState().lit && r.held == 0;
    if (r.serial == shownSerial && ! keysMoved && (r.pedal != 0) == pedal)
        return;
    shownSerial = r.serial;
    if ((r.pedal != 0) != pedal)
    {
        pedal = r.pedal != 0;
        repaint (readoutArea.getX(), controlsTop, readoutArea.getWidth(), rowH);
    }

    const bool held = r.held != 0;

    ni::ui::ChordReadout::State words;
    words.name = juce::String::fromUTF8 (r.name);
    words.degree = juce::String::fromUTF8 (r.degree);
    words.description = juce::String::fromUTF8 (r.description);
    words.notes = juce::String::fromUTF8 (r.notes_text);
    for (int i = 0; i < r.alternative_count && i < CD_ALTERNATIVES; ++i)
        words.alternatives.add (juce::String::fromUTF8 (r.alternatives[i]));
    words.held = held;
    readout.setState (words);

    /* The circle: the key from the parameters (Model::key), what sounds from
     * the reading. */
    const auto key = model.key();
    ni::ui::CircleOfFifths::State ring;
    ring.tonic = choiceOf (Param::tonic);
    ring.scale = key.scale;
    ring.lit = r.pitch_classes;
    ring.root = r.root;
    ring.dimmed = held;
    for (int pc = 0; pc < 12; ++pc)
        ring.names[(size_t) pc] = pitchName (model.write (60 + pc));
    ring.caption = signatureText (key.signature);
    circle.setState (ring);
    staff.setSignature (key.signature);

    /* The keyboard: what sounds; under Hold, after release, what was held. */
    ni::ui::Keyboard::State keys = keyboard.getState();
    keys.lit = held ? noteSet (r.notes) : noteSet (r.sounding);
    keys.dimmed = held;
    keys.bass = r.bass;
    keys.lowest = ni::ui::Keyboard::lowestToShow (keys.lit, keys.octaves, keys.lowest);
    keyboard.setState (keys);

    /* A new name is a mark on the history, where the chord began. */
    if (! held && words.name.isNotEmpty() && (r.kind == 3 || r.kind == 2))
        history.mark (r.now, words.name);
}

void Editor::paint (juce::Graphics& g)
{
    g.fillAll (c::bg000);
    uv::ground::paint (g, getLocalBounds().toFloat());

    const auto& label = uv::tok::type::label;
    const auto labelFont = uv::type::font (label);
    const auto& title = uv::tok::type::title;

    /* KEY over the mode select, in the circle's centre. */
    const auto centre = circle.centre().translated ((float) circleArea.getX(), (float) circleArea.getY());
    uv::type::draw (g, uv::type::cased (label, "Key"),
                    { centre.getX(), centre.getY(), centre.getWidth(), label.lineHeight }, labelFont, c::inkMuted,
                    juce::Justification::centred);

    /* SPELLING before its buttons; PEDAL at the end of the row. */
    uv::type::draw (g, uv::type::cased (label, "Spelling"),
                    { (float) readoutArea.getX(), (float) controlsTop, 72.0f, (float) rowH }, labelFont, c::inkMuted);
    const auto pedalWord = uv::type::cased (label, "Pedal");
    const float pedalW = uv::type::width (labelFont, pedalWord);
    const float pedalX = (float) right - pedalW;
    const juce::Rectangle<float> led { pedalX - uv::tok::space::space2 - lens,
                                       (float) controlsTop + ((float) rowH - lens) * 0.5f, lens, lens };
    juce::Path lensShape;
    lensShape.addEllipse (led);
    if (pedal)
        uv::light::glowLed (g, lensShape);
    g.setColour (pedal ? c::uv : c::inkDim);
    g.fillPath (lensShape);
    uv::type::draw (g, pedalWord, { pedalX, (float) controlsTop, pedalW + 1.0f, (float) rowH }, labelFont, c::inkMuted);

    /* HISTORY over its well, and the two wells. */
    uv::type::draw (g, uv::type::cased (title, "History"),
                    { (float) historyWell.getX(), (float) historyTop, 120.0f, (float) rowH }, uv::type::font (title),
                    c::inkMuted);
    for (const auto& well : { historyWell, keyboardWell })
    {
        g.setColour (well == historyWell ? c::bg200 : c::bg100);
        g.fillRect (well);
        g.setColour (c::line100);
        g.drawRect (well.toFloat(), uv::tok::stroke::strokeHair);
    }

    ni::ui::paintChildLights (g, *this);
}

void Editor::resized()
{
    circle.setBounds (circleArea);
    const auto centre = circle.centre().translated ((float) circleArea.getX(), (float) circleArea.getY()).toNearestInt();
    modeBox.setBounds (centre.getCentreX() - modeBox.idealWidth() / 2, centre.getY() + 20, modeBox.idealWidth(), rowH);

    readout.setBounds (readoutArea);
    spellingGroup.setBounds (spellingGroup.idealSize().withPosition (readoutArea.getX() + 80, controlsTop));
    holdToggle.setBounds (spellingGroup.getRight() + gap6, controlsTop, holdToggle.idealWidth(), rowH);

    const auto viewSize = viewGroup.idealSize();
    viewGroup.setBounds (viewSize.withPosition (right - viewSize.getWidth(), historyTop));
    spanBox.setBounds (viewGroup.getX() - gap6 - spanBox.idealWidth(), historyTop, spanBox.idealWidth(), rowH);
    zoomBox.setBounds (spanBox.getX() - gap6 - zoomBox.idealWidth(), historyTop, zoomBox.idealWidth(), rowH);

    staff.setBounds (historyWell);
    roll.setBounds (historyWell);
    keyboard.setBounds (keyboardArea);
    bar.setBounds (0, hintTop, width, ni::ui::Hint::height);
}

} // namespace ni::chord_detector
