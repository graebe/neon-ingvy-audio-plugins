// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Vocalize's editor against a model with nothing behind it
 * (vocalize_fakes.h): the window and its grid, what each control asks of the
 * host, the vowel chart following Vowel and Size, the envelope following the
 * audio and naming the engine that drew it, the captured voice -- its
 * buttons, the open panel, a recording dropped on the window, the chart's
 * caption and letters -- every line the window writes, and the window as a
 * picture: at its defaults, Spectral between E and I, and with a voice
 * captured.
 *
 * What only the real model can show -- the formants and the picture being
 * the engine's -- is tests/vocalize/vz_processor.cpp's.
 */
#include "Editor.h"
#include "InfoLines.h"
#include "Pointer.h"
#include "checks.h"
#include "snapshot.h"
#include "vocalize_fakes.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <vector>

using namespace ni::vz;
using ni::ui::gallery::Pointer;
using ni::vz::test::FakeModel;

namespace
{
struct Rig
{
    double ms = 0.0;
    FakeModel model;
    Editor editor { model, [this] { return ms; } };

    Rig()
    {
        editor.frame().ground().setReducedMotionQuery ([] { return false; });
        model.params.clear();
    }

    void frames (int n)
    {
        for (int i = 0; i < n; ++i)
        {
            ms += 1000.0 / 60.0;
            editor.tick (ms);
        }
    }
};

/* The knobs, panel by panel: Editor.h's layout. */
const std::vector<int> knobs { param::vowel, param::size, param::voice, param::vocal,
                               param::resonance, param::excite, param::mix, param::output };

/* The measured vowels of the singer the tests capture: A, I and U. */
constexpr std::array<bool, 5> sungAIU { true, false, true, false, true };
} // namespace

TEST_CASE ("vocalize: the window is its design's size, on its grid, and every line fits the bar")
{
    Rig rig;
    auto& e = rig.editor;
    CHECK (e.getWidth() == Editor::designWidth);
    CHECK (e.getHeight() == Editor::designHeight);
    CHECK (e.getWidth() == 888);
    CHECK (e.getHeight() == 532);
    NI_CHECK_INFO_LIMIT (e);

    /* The wells over the panels, on the same three columns. */
    CHECK (e.vowelChart().getBounds() == juce::Rectangle<int> (0, 0, Editor::column, Editor::plotH));
    CHECK (e.envelopePlot().getX() == e.panel (1).getX());
    CHECK (e.envelopePlot().getRight() == e.panel (2).getRight());
    CHECK (e.envelopePlot().getHeight() == Editor::plotH);
    CHECK (e.panel (0).getTitleText() == "Vowel");
    CHECK (e.panel (1).getTitleText() == "Vocal");
    CHECK (e.panel (2).getTitleText() == "Level");
    for (int p = 0; p < 3; ++p)
        CHECK (e.panel (p).getBounds() == juce::Rectangle<int> (p * (Editor::column + Editor::gutter), Editor::panelsY,
                                                                 Editor::column, Editor::panelH));

    /* Every control inside its panel's padding, none overlapping another. */
    std::vector<juce::Component*> controls;
    for (const int i : knobs)
        controls.push_back (&e.knob (i));
    controls.push_back (&e.modeSelect());
    controls.push_back (&e.captureButton());
    controls.push_back (&e.forgetButton());
    controls.push_back (&e.ceilingToggle());
    for (auto* c : controls)
    {
        const auto* panel = dynamic_cast<ni::ui::Panel*> (c->getParentComponent());
        REQUIRE (panel != nullptr);
        CHECK (panel->contentBounds().contains (c->getBounds()));
    }
    for (size_t a = 0; a < controls.size(); ++a)
        for (size_t b = a + 1; b < controls.size(); ++b)
            if (controls[a]->getParentComponent() == controls[b]->getParentComponent())
                CHECK_FALSE (controls[a]->getBounds().intersects (controls[b]->getBounds()));

    /* Which vowel and whose voice in the Vowel panel, the voice's buttons
     * under its knobs; how far and how in Vocal, Mode under its knobs; the
     * level's in Level, the Ceiling under them: and the dials on one line
     * across the window, the rows under them on another. */
    CHECK (e.knob (param::vowel).getParentComponent() == &e.panel (0));
    CHECK (e.knob (param::voice).getParentComponent() == &e.panel (0));
    CHECK (e.captureButton().getParentComponent() == &e.panel (0));
    CHECK (e.forgetButton().getParentComponent() == &e.panel (0));
    CHECK (e.captureButton().getY() > e.knob (param::vowel).getBottom());
    CHECK (e.forgetButton().getY() == e.captureButton().getY());
    CHECK (e.forgetButton().getX() > e.captureButton().getRight());
    CHECK (e.knob (param::resonance).getParentComponent() == &e.panel (1));
    CHECK (e.knob (param::vocal).getParentComponent() == &e.panel (1));
    CHECK (e.modeSelect().getParentComponent() == &e.panel (1));
    CHECK (e.modeSelect().getY() > e.knob (param::vocal).getBottom());
    CHECK (e.knob (param::output).getParentComponent() == &e.panel (2));
    CHECK (e.ceilingToggle().getParentComponent() == &e.panel (2));
    CHECK (e.ceilingToggle().getY() > e.knob (param::mix).getBottom());
    CHECK (e.ceilingToggle().getY() == e.modeSelect().getY());
    for (const int i : knobs)
        CHECK (e.knob (i).getY() == e.knob (param::vowel).getY());
}

TEST_CASE ("vocalize: each knob is its parameter, in the engine's words; Size and Output from 12 o'clock")
{
    Rig rig;
    auto& e = rig.editor;
    const std::pair<int, const char*> labels[] { { param::vowel, "Vowel" }, { param::size, "Size" },
                                                 { param::voice, "Voice" }, { param::resonance, "Resonance" },
                                                 { param::vocal, "Vocal" },
                                                 { param::excite, "Excite" }, { param::mix, "Mix" },
                                                 { param::output, "Output" } };
    for (const auto& entry : labels)
    {
        const int index = entry.first;
        const juce::String label (entry.second);
        CAPTURE (label);
        CHECK (e.knob (index).binding().parameter().getParameterIndex() == index);
        CHECK (e.knob (index).getLabel() == label);
        CHECK (e.knob (index).isBipolar() == (index == param::size || index == param::output));
    }
    CHECK (e.knob (param::vocal).getValueText() == "80 %");
    CHECK (e.knob (param::vowel).getValueText() == "A");
    CHECK (e.knob (param::size).getValueText() == "0 %");
    CHECK (e.knob (param::output).getValueText() == "0.0 dB");
    CHECK (e.knob (param::voice).getValueText() == "100 %");

    rig.model.set (param::vowel, 1.3);
    CHECK (e.knob (param::vowel).getValueText() == "E>I 30 %");
    rig.model.set (param::size, 30.0);
    CHECK (e.knob (param::size).getValueText() == "+30 %");
    rig.model.set (param::output, -3.0);
    CHECK (e.knob (param::output).getValueText() == "-3.0 dB");
}

TEST_CASE ("vocalize: the Vowel knob holds on each of the five vowels; no other knob holds anywhere")
{
    Rig rig;
    auto& vowel = rig.editor.knob (param::vowel);
    CHECK (vowel.getDetents() == std::vector<double> { 0.0, 0.25, 0.5, 0.75, 1.0 });
    for (const int i : knobs)
        if (i != param::vowel)
            CHECK_MESSAGE (rig.editor.knob (i).getDetents().empty(), i);

    /* From A, a drag up anywhere across E's hold lands on E exactly: a
     * quarter of the 200px travel, plus half A's hold and E's 14px. */
    for (const float dy : { 60.0f, 64.0f, 68.0f })
    {
        CAPTURE (dy);
        rig.model.set (param::vowel, 0.0);
        const auto centre = vowel.dial().getLocalBounds().getCentre().toFloat();
        Pointer p;
        p.down (vowel.dial(), centre);
        p.drag (centre.translated (0.0f, -dy));
        p.up();
        CHECK (rig.model.plain (param::vowel) == 1.0);
        CHECK (vowel.getValueText() == "E");
    }
}

TEST_CASE ("vocalize: Mode is a select of Time and Spectral, and shows what the host holds")
{
    Rig rig;
    auto& mode = rig.editor.modeSelect();
    CHECK (mode.binding().parameter().getParameterIndex() == param::mode);
    CHECK (mode.getOptions() == juce::StringArray { "Time", "Spectral" });
    CHECK (mode.getIndex() == 0);
    rig.model.set (param::mode, 1.0);
    CHECK (mode.getIndex() == 1);
    CHECK (mode.indexOfValue() == 1);
    rig.model.set (param::mode, 0.0);
    CHECK (mode.getIndex() == 0);
}

TEST_CASE ("vocalize: the vowel chart's dot follows Vowel, its caption in the engine's words")
{
    Rig rig;
    auto& chart = rig.editor.vowelChart();
    CHECK (chart.getCaption() == "VOWEL   A");
    CHECK (chart.dot() == chart.letter (0));

    for (int v = 1; v < 5; ++v)
    {
        rig.model.set (param::vowel, v);
        rig.frames (1);
        CHECK (chart.dot() == chart.letter (v));
    }

    rig.model.set (param::vowel, 1.3);
    rig.frames (1);
    CHECK (chart.getCaption() == "VOWEL   E>I 30 %");
    CHECK (chart.dot() != chart.letter (1));
    CHECK (chart.dot() != chart.letter (2));
    /* Between E and I on the chart: I is front and closed, above E and left
     * of it. */
    CHECK (chart.dot().x < chart.letter (1).x);
    CHECK (chart.dot().x > chart.letter (2).x);

    /* The vowel space as phoneticians draw it: I top left, A at the bottom,
     * U back at the top right. */
    CHECK (chart.letter (2).x < chart.letter (4).x);
    CHECK (chart.letter (0).y > chart.letter (2).y);
    CHECK (chart.letter (0).y > chart.letter (4).y);
}

TEST_CASE ("vocalize: Size slides the whole path along its diagonal; Resonance moves no formant")
{
    Rig rig;
    auto& chart = rig.editor.vowelChart();
    rig.model.set (param::vowel, 2.5);
    rig.frames (1);
    const auto at = [&chart]
    {
        std::vector<juce::Point<float>> p;
        for (int k = 0; k < 5; ++k)
            p.push_back (chart.letter (k));
        p.push_back (chart.dot());
        return p;
    };
    const auto middle = at();

    /* Bigger: every formant lower -- F2 to the right, F1 up. */
    rig.model.set (param::size, 100.0);
    rig.frames (1);
    const auto bigger = at();
    for (size_t k = 0; k < middle.size(); ++k)
    {
        CAPTURE (k);
        CHECK (bigger[k].x > middle[k].x);
        CHECK (bigger[k].y < middle[k].y);
    }

    /* Smaller: the other way. */
    rig.model.set (param::size, -100.0);
    rig.frames (1);
    const auto smaller = at();
    for (size_t k = 0; k < middle.size(); ++k)
    {
        CAPTURE (k);
        CHECK (smaller[k].x < middle[k].x);
        CHECK (smaller[k].y > middle[k].y);
    }

    rig.model.set (param::resonance, 95.0);
    rig.frames (1);
    CHECK (at() == smaller);
}

TEST_CASE ("vocalize: the envelope waits for audio, then names its engine -- Time, or Spectral and how late")
{
    Rig rig;
    auto& plot = rig.editor.envelopePlot();
    CHECK_FALSE (plot.hasPicture());
    CHECK (plot.getCaption() == "ENVELOPE");

    rig.model.play (false, 0, 44100.0);
    rig.frames (1);
    CHECK (plot.hasPicture());
    CHECK (plot.getCaption() == "ENVELOPE   TIME");

    /* 1024 samples at 44.1 kHz: 23.2 ms. */
    rig.model.play (true, 1024, 44100.0);
    rig.frames (1);
    CHECK (plot.getCaption() == "ENVELOPE   SPECTRAL   23 MS LATE");

    /* 2048 at 96 kHz: 21.3 ms. */
    rig.model.play (true, 2048, 96000.0);
    rig.frames (1);
    CHECK (plot.getCaption() == "ENVELOPE   SPECTRAL   21 MS LATE");

    rig.model.play (false, 0, 96000.0);
    rig.frames (1);
    CHECK (plot.getCaption() == "ENVELOPE   TIME");

    /* Its frequencies on the plot's scale: 50 Hz at the left, 10 kHz at the
     * right, 1 kHz between. */
    CHECK (plot.xOf (50.0) < plot.xOf (1000.0));
    CHECK (plot.xOf (1000.0) < plot.xOf (10000.0));
    CHECK (plot.xOf (20.0) == plot.xOf (50.0));
}

TEST_CASE ("vocalize: every line the window writes is in it, each where it belongs")
{
    Rig rig;
    auto& e = rig.editor;
    const auto lines = ni::ui::collectInfo (e);
    const auto has = [&lines] (const juce::String& line) { return std::find (lines.begin(), lines.end(), line) != lines.end(); };
    for (const auto& line : { info::vowelPanel, info::vocalPanel, info::levelPanel, info::vowelChart, info::envelopePlot,
                              info::vowel, info::size, info::voice, info::capture, info::forget, info::resonance,
                              info::vocal, info::excite, info::mode, info::mix, info::ceiling,
                              info::output, info::vowelValue, info::percentValue, info::dbValue, info::motion,
                              info::signature })
        CHECK_MESSAGE (has (line), line.str());

    CHECK (ni::ui::infoOf (e.knob (param::vowel)) == info::vowel.str());
    CHECK (ni::ui::infoOf (e.knob (param::size)) == info::size.str());
    CHECK (ni::ui::infoOf (e.knob (param::output)) == info::output.str());
    CHECK (ni::ui::infoOf (e.modeSelect()) == info::mode.str());
    CHECK (ni::ui::infoOf (e.knob (param::voice)) == info::voice.str());
    CHECK (ni::ui::infoOf (e.captureButton()) == info::capture.str());
    CHECK (ni::ui::infoOf (e.forgetButton()) == info::forget.str());
    CHECK (ni::ui::infoOf (e.ceilingToggle()) == info::ceiling.str());
    CHECK (ni::ui::infoOf (e.vowelChart()) == info::vowelChart.str());
    CHECK (ni::ui::infoOf (e.envelopePlot()) == info::envelopePlot.str());

    /* And held to the bar in the other states too. */
    rig.model.set (param::mode, 1.0);
    rig.model.play (true, 1024, 44100.0);
    rig.frames (2);
    NI_CHECK_INFO_LIMIT (e);
    rig.model.land (Voice::Status::captured, "My Voice.wav", sungAIU);
    rig.frames (2);
    NI_CHECK_INFO_LIMIT (e);
}


TEST_CASE ("vocalize: Ceiling is a switch bound to its parameter, on by default")
{
    Rig rig;
    auto& t = rig.editor.ceilingToggle();
    CHECK (t.binding().parameter().getParameterIndex() == param::ceiling);
    CHECK (t.getLabel() == "Ceiling");
    CHECK (t.isOn());

    Pointer p;
    p.click (t, t.housing().getCentre());
    CHECK (rig.model.plain (param::ceiling) == 0.0);
    CHECK_FALSE (t.isOn());
    CHECK (rig.model.params.log().startsWith ("begin " + juce::String (param::ceiling)));

    /* And from the host's side. */
    rig.model.set (param::ceiling, 1.0);
    CHECK (t.isOn());
}

/* ------------------------------------------------------------ the voice -- */

TEST_CASE ("vocalize: the voice's buttons say where a capture is, and offer only what can be done")
{
    Rig rig;
    auto& capture = rig.editor.captureButton();
    auto& forget = rig.editor.forgetButton();
    const auto shows = [&] (const char* text, bool captureOn, const char* second, bool secondOn)
    {
        /* The words as the editor's source spells them, in UTF-8. */
        CHECK (capture.getText() == juce::String::fromUTF8 (text));
        CHECK (capture.isEnabled() == captureOn);
        CHECK (forget.getText() == second);
        CHECK (forget.isEnabled() == secondOn);
    };

    shows ("Capture…", true, "Forget", false);
    rig.model.capture (juce::File ("/recordings/My Voice.wav"));
    rig.frames (1);
    shows ("Capturing…", false, "Forget", false);
    rig.model.land (Voice::Status::captured, "My Voice.wav", sungAIU);
    rig.frames (1);
    shows ("Capture…", true, "Forget", true);
    /* A capture that fails keeps the voice sung before it, which can still
     * be forgotten. */
    rig.model.land (Voice::Status::unreadable, "notes.wav", {});
    rig.frames (1);
    shows ("Capture…", true, "Forget", true);
    rig.model.land (Voice::Status::unsung, "silence.wav", {});
    rig.frames (1);
    shows ("Capture…", true, "Forget", true);
    rig.model.land (Voice::Status::noVowels, "hum.wav", {});
    rig.frames (1);
    shows ("Capture…", true, "Forget", true);

    /* Forget asks the model, and then offers the voice back: Restore. */
    rig.model.land (Voice::Status::captured, "My Voice.wav", sungAIU);
    rig.frames (1);
    Pointer p;
    p.click (forget, forget.getLocalBounds().getCentre().toFloat());
    CHECK (rig.model.forgotten == 1);
    rig.frames (1);
    shows ("Capture…", true, "Restore", true);

    /* Restore asks the model to bring it back, and Forget is Forget again. */
    p.click (forget, forget.getLocalBounds().getCentre().toFloat());
    CHECK (rig.model.restored == 1);
    CHECK (rig.model.forgotten == 1);
    rig.frames (1);
    CHECK (rig.model.voice().sungName == "My Voice.wav");
    shows ("Capture…", true, "Forget", true);

    /* Forgotten, then another voice sung: nothing to restore, only to
     * forget. */
    p.click (forget, forget.getLocalBounds().getCentre().toFloat());
    rig.frames (1);
    shows ("Capture…", true, "Restore", true);
    rig.model.land (Voice::Status::captured, "Take Two.wav", sungAIU);
    rig.frames (1);
    shows ("Capture…", true, "Forget", true);
    p.click (forget, forget.getLocalBounds().getCentre().toFloat());
    rig.frames (1);
    /* ... and once that one is forgotten too, it is the one offered back. */
    shows ("Capture…", true, "Restore", true);
    rig.model.forgetVoice();
    rig.frames (1);
    shows ("Capture…", true, "Forget", false);
}

TEST_CASE ("vocalize: Capture… opens the panel on the recordings the plugin reads, and captures the one chosen")
{
    Rig rig;
    auto& model = rig.model;
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("vocalize_editor", {}, false);
    REQUIRE (dir.createDirectory());
    const auto take = dir.getChildFile ("take.wav");
    REQUIRE (take.replaceWithText ("RIFF"));

    juce::String asked;
    juce::File answer = take;
    int opened = 0;
    rig.editor.setChooser ([&] (const juce::String& wildcard, std::function<void (const juce::File&)> then)
    {
        ++opened;
        asked = wildcard;
        then (answer);
    });
    auto& capture = rig.editor.captureButton();
    Pointer p;
    p.click (capture, capture.getLocalBounds().getCentre().toFloat());
    CHECK (opened == 1);
    CHECK (asked == model.recordingWildcard());
    REQUIRE (model.captures.size() == 1);
    CHECK (model.captures[0] == take);

    /* The panel closed with nothing chosen, or on a file that is not there:
     * nothing is captured. */
    model.land (Voice::Status::none, {}, {});
    rig.frames (1);
    for (const auto& none : { juce::File(), dir.getChildFile ("gone.wav") })
    {
        answer = none;
        p.click (capture, capture.getLocalBounds().getCentre().toFloat());
        CHECK (model.captures.size() == 1);
    }
    CHECK (opened == 3);
    dir.deleteRecursively();
}

TEST_CASE ("vocalize: a recording dragged over the window outlines the chart, and dropped is captured")
{
    Rig rig;
    auto& e = rig.editor;
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("vocalize_editor", {}, false);
    REQUIRE (dir.createDirectory());
    const auto wav = dir.getChildFile ("take.wav");
    const auto aif = dir.getChildFile ("Take Two.AIF");
    const auto text = dir.getChildFile ("lyrics.txt");
    for (const auto& f : { wav, aif, text })
        REQUIRE (f.replaceWithText ("x"));
    const auto paths = [] (std::initializer_list<juce::File> files)
    {
        juce::StringArray out;
        for (const auto& f : files)
            out.add (f.getFullPathName());
        return out;
    };

    /* Interested in a recording the plugin can read, and only one that is
     * there. */
    CHECK (e.isInterestedInFileDrag (paths ({ wav })));
    CHECK (e.isInterestedInFileDrag (paths ({ aif })));
    CHECK (e.isInterestedInFileDrag (paths ({ text, wav })));
    CHECK_FALSE (e.isInterestedInFileDrag (paths ({ text })));
    CHECK_FALSE (e.isInterestedInFileDrag (paths ({ dir.getChildFile ("gone.wav") })));
    CHECK_FALSE (e.isInterestedInFileDrag (paths ({ dir })));

    CHECK_FALSE (e.vowelChart().isDropping());
    e.fileDragEnter (paths ({ wav }), 10, 10);
    CHECK (e.vowelChart().isDropping());
    e.fileDragExit (paths ({ wav }));
    CHECK_FALSE (e.vowelChart().isDropping());

    /* Dropped: the first recording among the files is captured. */
    e.fileDragEnter (paths ({ text, aif }), 10, 10);
    e.filesDropped (paths ({ text, aif }), 10, 10);
    CHECK_FALSE (e.vowelChart().isDropping());
    REQUIRE (rig.model.captures.size() == 1);
    CHECK (rig.model.captures[0] == aif);

    /* Nothing a recording among them: nothing captured. */
    e.filesDropped (paths ({ text }), 10, 10);
    CHECK (rig.model.captures.size() == 1);
    dir.deleteRecursively();
}

TEST_CASE ("vocalize: the chart's caption names the voice -- capturing, captured, or what went wrong")
{
    Rig rig;
    auto& chart = rig.editor.vowelChart();
    rig.model.set (param::vowel, 1.3);
    rig.frames (1);
    CHECK (chart.getCaption() == "VOWEL   E>I 30 %");

    rig.model.capture (juce::File ("/recordings/My Voice.wav"));
    rig.frames (1);
    CHECK (chart.getCaption() == "VOWEL   CAPTURING MY VOICE");
    rig.model.land (Voice::Status::captured, "My Voice.wav", sungAIU);
    rig.frames (1);
    CHECK (chart.getCaption() == "VOWEL   E>I 30 %   MY VOICE");
    rig.model.set (param::vowel, 2.0);
    rig.frames (1);
    CHECK (chart.getCaption() == "VOWEL   I   MY VOICE");

    /* A long name is cut to fit the well. */
    rig.model.land (Voice::Status::captured, "A Very Long Recording Name.wav", sungAIU);
    rig.frames (1);
    CHECK (chart.getCaption() == "VOWEL   I   A VERY LONG R…");

    rig.model.land (Voice::Status::unreadable, "notes.wav", {});
    rig.frames (1);
    CHECK (chart.getCaption() == "VOWEL   CANNOT READ NOTES");
    rig.model.land (Voice::Status::unsung, "silence.aif", {});
    rig.frames (1);
    CHECK (chart.getCaption() == "VOWEL   NO SINGING IN SILENCE");
    rig.model.land (Voice::Status::noVowels, "hum.wav", {});
    rig.frames (1);
    CHECK (chart.getCaption() == "VOWEL   NO VOWELS IN HUM");
    rig.model.forgetVoice();
    rig.frames (1);
    CHECK (chart.getCaption() == "VOWEL   I");
}

TEST_CASE ("vocalize: a captured voice moves the chart's vowels, as far as Voice says, and inks the ones measured")
{
    Rig rig;
    auto& chart = rig.editor.vowelChart();
    rig.frames (1);
    std::array<juce::Point<float>, 5> before;
    for (int k = 0; k < 5; ++k)
        before[(size_t) k] = chart.letter (k);

    /* A bigger singer: every vowel lower, to the right and up. */
    rig.model.land (Voice::Status::captured, "My Voice.wav", sungAIU, 0.85);
    rig.frames (1);
    for (int k = 0; k < 5; ++k)
    {
        CAPTURE (k);
        CHECK (chart.letter (k).x > before[(size_t) k].x);
        CHECK (chart.letter (k).y < before[(size_t) k].y);
    }
    rig.model.set (param::voice, 0.0);
    rig.frames (1);
    for (int k = 0; k < 5; ++k)
        CHECK (chart.letter (k) == before[(size_t) k]);
    rig.model.set (param::voice, 100.0);
    rig.frames (1);

    /* The letters of the vowels measured are drawn brighter than the rest:
     * the brightest pixel of each letter's box. */
    const auto image = ni::ui::test::render (chart);
    const auto centre = chart.contentBounds().getCentreX();
    const auto brightest = [&] (int k)
    {
        const auto p = chart.letter (k);
        const float dx = p.x < centre ? -12.0f : 12.0f;
        const auto box = juce::Rectangle<float> (p.x + dx - 8.0f, p.y - 8.0f, 16.0f, 16.0f).getSmallestIntegerContainer()
                             .getIntersection (image.getBounds());
        float most = 0.0f;
        for (int y = box.getY(); y < box.getBottom(); ++y)
            for (int x = box.getX(); x < box.getRight(); ++x)
                most = std::max (most, image.getPixelAt (x, y).getPerceivedBrightness());
        return most;
    };
    for (const int sung : { 0, 2, 4 })
        for (const int unsung : { 1, 3 })
        {
            CAPTURE (sung);
            CAPTURE (unsung);
            CHECK (brightest (sung) > brightest (unsung) + 0.05f);
        }

    /* As bright as the capture was sure of each: I measured from frames that
     * disagreed is dimmer than A and U, which were sure. */
    rig.model.land (Voice::Status::captured, "My Voice.wav", sungAIU, 0.85, { 1.0, 0.0, 0.05, 0.0, 1.0 });
    rig.frames (1);
    const auto unsure = ni::ui::test::render (chart);
    const auto brightestIn = [&] (const juce::Image& img, int k)
    {
        const auto p = chart.letter (k);
        const float dx = p.x < centre ? -12.0f : 12.0f;
        const auto box = juce::Rectangle<float> (p.x + dx - 8.0f, p.y - 8.0f, 16.0f, 16.0f).getSmallestIntegerContainer()
                             .getIntersection (img.getBounds());
        float most = 0.0f;
        for (int y = box.getY(); y < box.getBottom(); ++y)
            for (int x = box.getX(); x < box.getRight(); ++x)
                most = std::max (most, img.getPixelAt (x, y).getPerceivedBrightness());
        return most;
    };
    CHECK (brightestIn (unsure, 2) + 0.05f < brightestIn (unsure, 0));
    CHECK (brightestIn (unsure, 2) + 0.05f < brightestIn (unsure, 4));
    CHECK (brightestIn (unsure, 2) + 0.05f < brightest (2));
}

NI_SNAPSHOT_TEST ("vocalize: the defaults, a sound playing through Time")
{
    Rig rig;
    rig.model.play (false, 0, 44100.0);
    rig.frames (60);
    NI_CHECK_SNAPSHOT (rig.editor, "vocalize-default");
}

NI_SNAPSHOT_TEST ("vocalize: Spectral, between E and I, a bigger voice")
{
    Rig rig;
    rig.model.set (param::mode, 1.0);
    rig.model.set (param::vocal, 85.0);
    rig.model.set (param::vowel, 1.3);
    rig.model.set (param::size, 30.0);
    rig.model.set (param::resonance, 65.0);
    rig.model.set (param::excite, 40.0);
    rig.model.set (param::mix, 90.0);
    rig.model.set (param::output, -3.0);
    rig.model.play (true, 1024, 44100.0);
    rig.frames (60);
    NI_CHECK_SNAPSHOT (rig.editor, "vocalize-spectral");
}

NI_SNAPSHOT_TEST ("vocalize: a voice captured, singing I, A I U measured")
{
    Rig rig;
    rig.model.set (param::vowel, 2.0);
    rig.model.set (param::vocal, 90.0);
    rig.model.land (Voice::Status::captured, "My Voice.wav", sungAIU);
    rig.model.play (false, 0, 48000.0);
    rig.frames (60);
    NI_CHECK_SNAPSHOT (rig.editor, "vocalize-captured");
}
