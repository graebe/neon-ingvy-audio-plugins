// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Trance Gate's plugin, faked for its editor's tests: the fifteen host
 * parameters exactly as the iPlug2 build declares them, a model whose
 * snapshots the test sets and whose commands it reads back, and the clipboard
 * and file panels the window verbs use.
 *
 * THE PARAMETERS are Params.cpp's, in its order -- the VST3 IDs the spike's
 * tests/fixtures/iplug2/NITranceGate/parameters.json lists (Slot 0 ... Fade
 * Dir 14; Bypass is the host's): the same names, ranges, steps, defaults and
 * display text ("1/16", "100.00 %", "Off", "Hard", "In"). A fake in another
 * order would test another plugin.
 *
 * A COMMAND SHOWS IN A LATER SNAPSHOT, as the processor's do: setStep and the
 * rest are logged at once (`edits`) and applied to the pattern the editor
 * reads only on publish(), which is the engine's next block. So a test sees
 * what the editor asked for, and the editor cannot lean on reading its own
 * edit back before the engine took it.
 *
 * THE FADE'S LEVELS, THE DETENTS AND THE STAGES' TEXT stand in for the
 * engine's with the web editor's ports (lib/fade.js, which a table the engine
 * generates pins; rates.rs's detents; Params.cpp's FormatStage and
 * ParseStage). They are the test's stand-ins, not the editor's: the editor
 * draws and shows whatever the model hands it.
 */
#pragma once

#include "Clipboard.h"
#include "FilePanels.h"
#include "Model.h"
#include "fakes.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ni::tg::test
{

/* The engine's rate labels and beats (rates.rs), in its order. */
inline juce::StringArray rateLabels()
{
    return { "1/1T", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64", "1/128" };
}

inline double rateBeats (int index)
{
    static const double beats[] { 8.0 / 3.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0,
                                  0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625, 0.03125 };
    return beats[juce::jlimit (0, 12, index)];
}

/* "%.2f %%", Params.cpp's kPctDisplay. */
inline juce::String percentText (float v)
{
    return juce::String (std::round (v * 100.0f) / 100.0f + 0.0f, 2) + " %";
}

/*
 * AN INTEGER THE HOST STEPS THROUGH, as the processor's ni::Parameter declares
 * Slot and Length (plugins/_shared/juce/Parameter.cpp): discrete, as iPlug2
 * declared both, its steps counted -- and NO LIST OF ITS VALUES, which that
 * class gives only a choice or a toggle. JUCE's own int would build one from
 * its text, and a fake that kind hid UT3: the plugin's Slot select empty,
 * while this one showed "1".
 */
class SteppedInt final : public juce::AudioParameterInt
{
public:
    using AudioParameterInt::AudioParameterInt;
    bool isDiscrete() const override { return true; }
    juce::StringArray getAllValueStrings() const override { return {}; }
};

/* The fifteen, as Params.cpp declares them and the spike's processor builds
 * them: the percentages continuous, as iPlug2 left them. */
inline void addParameters (ni::ui::test::FakeParameters& p)
{
    const auto number = [] (const juce::String& s) { return s.getFloatValue(); };
    const auto pct = [&] (const char* id, const char* name, float lo, float hi, float def)
    {
        p.addFloat (id, name, { lo, hi }, def, percentText, number);
    };
    const auto stepped = [&] (const char* id, const char* name, int lo, int hi, int def)
    {
        p.add (std::make_unique<SteppedInt> (juce::ParameterID { id, 1 }, name, lo, hi, def));
    };
    /* A switch whose two states are words of its own ("Hard", "Soft"). */
    const auto words = [&] (const char* id, const char* name, const char* off, const char* on)
    {
        auto attributes = juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction ([off, on] (bool v, int) { return juce::String (v ? on : off); })
            .withValueFromStringFunction ([on] (const juce::String& s) { return s == on; });
        p.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, false, attributes));
    };

    stepped ("slot", "Slot", 1, 8, 1);
    stepped ("length", "Length", 1, 128, 16);
    p.addChoice ("rate", "Rate", rateLabels(), 7);
    words ("legato", "Join Neighbors", "Off", "On");
    p.addChoice ("timeMode", "Env Time", { "ms", "%" }, 0);
    p.addChoice ("curve", "Env Curve", { "Linear", "Exponential", "S-Curve" }, 0);
    pct ("amount", "Amount", 0.0f, 100.0f, 100.0f);
    pct ("width", "Width", 5.0f, 100.0f, 100.0f);
    pct ("attack", "Attack", 0.0f, 200.0f, 1.6f);
    pct ("decay", "Decay", 0.0f, 200.0f, 16.0f);
    pct ("sustain", "Sustain", 0.0f, 100.0f, 100.0f);
    pct ("release", "Release", 0.0f, 200.0f, 16.0f);
    pct ("fade", "Fade", 0.0f, 100.0f, 100.0f);
    words ("fadeSoft", "Fade Shape", "Hard", "Soft");
    p.addChoice ("fadeDir", "Fade Dir", { "In", "Out" }, 0);
}

/* ------------------------------------------------------- the fade's levels -- */

/* lib/fade.js's fadeWeight: one arrival's weight. */
inline float fadeWeight (int rank, int n, float fade, bool soft)
{
    if (rank < 1 || n < 1)
        return 0.0f;
    const float v = juce::jlimit (0.0f, 1.0f, fade * (float) n - (float) (rank - 1));
    if (soft)
        return v;
    return v >= 1.0f - 1.0e-6f ? 1.0f : 0.0f;
}

/* lib/fade.js's fadeWeights, into the pattern's levels. */
inline void fadeLevels (Pattern& p, float fade, bool soft, bool out)
{
    const int n = juce::jlimit (1, maxSteps, p.length);
    const auto on = [&p] (int i) { return p.steps[(std::size_t) i] != StepMode::off; };
    const bool arrivingOn = ! out;
    int count = 0;
    for (int i = 0; i < n; ++i)
        count += on (i) == arrivingOn ? 1 : 0;
    p.levels.fill (0.0f);
    for (int i = 0; i < n; ++i)
    {
        if (on (i) != arrivingOn)
        {
            p.levels[(std::size_t) i] = on (i) ? 1.0f : 0.0f;
            continue;
        }
        const float w = fadeWeight (p.orders[(std::size_t) i], count, fade, soft);
        p.levels[(std::size_t) i] = arrivingOn ? w : 1.0f - w;
    }
}

/* ---------------------------------------------------------------- model -- */

class FakeModel final : public Model
{
public:
    FakeModel()
    {
        addParameters (common.params);
        for (int i = 0; i < 16; ++i)
            next.steps[(std::size_t) i] = i % 2 == 0 ? StepMode::on : StepMode::off;
        next.depths.fill (1.0f);
        rankAll (next);
        publish();
    }

    ni::ui::test::FakeEditorModel common;
    ni::ui::test::FakeParameters& params = common.params;

    int numParameters() const override { return common.numParameters(); }
    juce::RangedAudioParameter& parameter (int index) override { return common.parameter (index); }
    int takeRings (float* s, int capacity) override { return common.takeRings (s, capacity); }
    bool motion() const override { return common.motion(); }
    void setMotion (bool on) override { common.setMotion (on); }

    /* ---- snapshots, as the test sets them */

    /* What the editor reads, and what the commands change until publish(). */
    Pattern shown, next;
    Transport engineTransport;
    GateCurve gateCurve;
    EnvelopeCurves envelopeCurves;
    Capture signal;
    /* The host's meter, which the detents count in. */
    int meterNum = 4, meterDen = 4;

    /* The engine's next block: the commands so far, and the fade's levels
     * from the host parameters now. */
    void publish()
    {
        fadeLevels (next, plain (param::fade) / 100.0f, params[param::fadeSoft].getValue() >= 0.5f,
                    params[param::fadeDir].getValue() >= 0.5f);
        shown = next;
    }

    /* Every arriving kind ranked in position order, 1..n: a fresh pattern's. */
    static void rankAll (Pattern& p)
    {
        int hits = 0, holes = 0;
        for (int i = 0; i < maxSteps; ++i)
            p.orders[(std::size_t) i] = p.steps[(std::size_t) i] != StepMode::off ? ++hits : ++holes;
    }

    const Pattern& pattern() override
    {
        ++patternReads;
        return shown;
    }
    Transport transport() override { return engineTransport; }
    const GateCurve& gate() override { return gateCurve; }
    const EnvelopeCurves& envelope() override { return envelopeCurves; }
    const Capture& capture() override { return signal; }

    /* rates.rs's detents: half a bar, one, two and four, whole and in range. */
    std::vector<int> lengthDetents() override
    {
        const int rate = juce::roundToInt (params[param::rate].getValue() * 12.0f);
        const double perBar = meterNum * 4.0 / meterDen / rateBeats (rate);
        std::vector<int> out;
        for (const double bars : { 0.5, 1.0, 2.0, 4.0 })
        {
            const double steps = perBar * bars, whole = std::round (steps);
            if (std::abs (steps - whole) < 1.0e-6 && whole >= 1.0 && whole <= maxSteps)
                out.push_back ((int) whole);
        }
        return out;
    }

    /* The gate's open time: one step at the Rate and tempo, times Width. */
    double widthMs() const { return engineTransport.msPerStep * plain (param::width) / 100.0; }
    bool stagesInMs() const { return params[param::timeMode].getValue() < 0.5f; }

    /* Params.cpp's FormatStage. */
    juce::String stageText (int index) override
    {
        const double pct = plain (index);
        if (stagesInMs() && widthMs() > 0.0)
            return juce::String (pct / 100.0 * widthMs(), 1) + " ms";
        return juce::String (pct, 2) + " %";
    }

    /* Params.cpp's ParseStage, normalised over 0..200 %. */
    std::optional<float> stageValue (int index, const juce::String& typed) override
    {
        juce::ignoreUnused (index);
        const auto text = typed.trim();
        if (text.isEmpty() || (! juce::CharacterFunctions::isDigit (text[0]) && text[0] != '.' && text[0] != '-'))
            return std::nullopt;
        const double v = text.getDoubleValue();
        const bool saysMs = text.contains ("ms"), saysPct = text.contains ("%");
        const bool asMs = saysMs || (stagesInMs() && ! saysPct);
        double pct = asMs ? (widthMs() > 0.0 ? v / widthMs() * 100.0 : 0.0) : v;
        pct = juce::jlimit (0.0, 200.0, pct);
        return (float) (pct / 200.0);
    }

    /* ---- commands, logged as words: "step 3 on", "depth 3 0.50" */

    std::vector<std::string> edits;
    int patternReads = 0;

    void setStep (int index, StepMode mode) override
    {
        const char* names[] { "off", "on", "tie" };
        edits.push_back ("step " + std::to_string (index) + " " + names[(int) mode]);
        next.steps[(std::size_t) index] = mode;
        next.cursor = index;
    }
    void setDepth (int index, float amount) override
    {
        edits.push_back ("depth " + std::to_string (index) + " " + juce::String (amount, 2).toStdString());
        next.depths[(std::size_t) index] = amount;
    }
    void setOrder (int index, int rank) override
    {
        edits.push_back ("order " + std::to_string (index) + " " + std::to_string (rank));
    }
    void randomize() override { edits.push_back ("randomize"); }
    void shuffleOrder() override { edits.push_back ("shuffle"); }

    /* The edits so far as one line, and none after. */
    std::string takeEdits()
    {
        std::string out;
        for (const auto& e : edits)
            out += (out.empty() ? "" : ", ") + e;
        edits.clear();
        return out;
    }

    /* ---- slots as text */

    /* What exportText hands over, per kind; empty is "not ready". */
    std::string slotText = "{\"nitgslot\":1}", bankText = "{\"nitgbank\":1}";
    /* What the next paste or import comes to, and the texts they were given. */
    Transfer transferResult { Transfer::Kind::slot, {} };
    std::vector<std::string> pasted, imported;
    juce::File folder;

    std::string exportText (bool bank) override { return bank ? bankText : slotText; }
    Transfer importText (const std::string& text) override
    {
        imported.push_back (text);
        return transferResult;
    }
    Transfer paste (const std::string& text) override
    {
        pasted.push_back (text);
        return transferResult;
    }
    juce::File lastFolder() override { return folder; }
    void setLastFolder (const juce::File& f) override { folder = f; }

    /* ---- the host's side */

    /* A parameter's value in its own units. */
    float plain (int index) const
    {
        auto& p = params[index];
        return p.convertFrom0to1 (p.getValue());
    }
    /* Sets a parameter as the host would, in its own units, and forgets it. */
    void set (int index, float plainValue)
    {
        auto& p = params[index];
        p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
        params.clear();
    }
};

/* ------------------------------------------------- renders, for pictures -- */

/*
 * Curves shaped like the engine's, for the goldens and the plot tests: a
 * linear attack, decay to Sustain, the gate closing at Width and a linear
 * release, per step at its amount and level, a tie carrying the step before
 * it on. Not the engine's DSP and not meant to be: the editor draws whatever
 * curve it is handed, and these only have to look like one.
 */
inline float stageLevel (double t, double a, double d, double s, double gate, double r)
{
    const auto attackDecay = [&] (double u)
    {
        if (u < a)
            return a > 0.0 ? u / a : 1.0;
        if (u < a + d)
            return 1.0 - (1.0 - s) * (d > 0.0 ? (u - a) / d : 1.0);
        return s;
    };
    if (t < gate)
        return (float) attackDecay (t);
    const double at = attackDecay (gate);
    if (r <= 0.0 || t >= gate + r)
        return 0.0f;
    return (float) (at * (1.0 - (t - gate) / r));
}

inline void renderCurves (FakeModel& m, int perStep = 32)
{
    const double width = m.plain (param::width) / 100.0;
    const double a = m.plain (param::attack) / 100.0 * width, d = m.plain (param::decay) / 100.0 * width,
                 r = m.plain (param::release) / 100.0 * width, s = m.plain (param::sustain) / 100.0;
    const auto& p = m.shown;
    const int n = juce::jlimit (1, maxSteps, p.length);

    m.gateCurve.steps = n;
    m.gateCurve.perStep = perStep;
    m.gateCurve.values.assign ((std::size_t) (n * perStep), 0.0f);
    float carry = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const auto mode = p.steps[(std::size_t) i];
        const float level = p.depths[(std::size_t) i] * p.levels[(std::size_t) i];
        for (int k = 0; k < perStep; ++k)
        {
            const double t = (double) k / perStep;
            float v = 0.0f;
            if (mode == StepMode::tie && i > 0)
                v = carry;
            else if (level > 0.0f)
                v = level * stageLevel (t, a, d, s, width, r);
            m.gateCurve.values[(std::size_t) (i * perStep + k)] = v;
        }
        carry = m.gateCurve.values[(std::size_t) (i * perStep + perStep - 1)];
        if (mode != StepMode::tie && level > 0.0f)
            carry = level * stageLevel (std::min (width, 0.999), a, d, s, width, r);
    }
    ++m.gateCurve.serial;

    const int steps = 3;
    m.envelopeCurves.steps = steps;
    m.envelopeCurves.perStep = perStep;
    m.envelopeCurves.gated.resize ((std::size_t) (steps * perStep));
    m.envelopeCurves.ghost.resize ((std::size_t) (steps * perStep));
    for (int k = 0; k < steps * perStep; ++k)
    {
        const double t = (double) k / perStep;
        m.envelopeCurves.gated[(std::size_t) k] = stageLevel (t, a, d, s, width, r);
        m.envelopeCurves.ghost[(std::size_t) k] = stageLevel (t, a, d, s, 1.0e9, r);
    }
    ++m.envelopeCurves.serial;
}

/* A capture of one cycle: a busy input, gated by the render, written up to
 * column `head`. */
inline void fillCapture (FakeModel& m, int columns, int head, double cycleMs, float level = 0.8f)
{
    auto& c = m.signal;
    c.columns = columns;
    c.cycleMs = cycleMs;
    c.head = head;
    c.data.assign ((std::size_t) (columns * Capture::stride), 0.0f);
    const auto& g = m.gateCurve.values;
    for (int i = 0; i < columns; ++i)
    {
        const float in = level * (float) (0.55 + 0.45 * std::abs (std::sin (i * 0.37) * std::cos (i * 0.11)));
        const float gain = g.empty() ? 1.0f : g[(std::size_t) ((double) i / columns * (double) g.size())];
        auto* col = c.data.data() + i * Capture::stride;
        col[0] = -in;
        col[1] = in;
        col[2] = -in * gain;
        col[3] = in * gain;
    }
    ++c.serial;
}

/* ------------------------------------------------- clipboard and panels -- */

class FakeClipboard final : public Clipboard
{
public:
    std::optional<std::string> text;
    bool writable = true;
    int writes = 0;

    std::optional<std::string> read() override { return text; }
    bool write (const std::string& t) override
    {
        ++writes;
        if (! writable)
            return false;
        text = t;
        return true;
    }
};

/*
 * Panels a test answers, over files that live in memory: `files` by full
 * path. A panel asked for is held in `asked` until answer() or cancel().
 */
class FakeFilePanels final : public FilePanels
{
public:
    struct Ask
    {
        bool save = false;
        juce::File folder;
        juce::String name, patterns;
        Chosen chosen;
    };

    std::optional<Ask> asked;
    bool canShow = true;
    bool writable = true;
    std::map<juce::String, std::string> files;
    std::vector<std::size_t> readLimits;

    bool save (const juce::File& folder, const juce::String& name, const juce::String& patterns,
               Chosen chosen) override
    {
        if (! canShow)
            return false;
        asked = Ask { true, folder, name, patterns, std::move (chosen) };
        return true;
    }
    bool open (const juce::File& folder, const juce::String& patterns, Chosen chosen) override
    {
        if (! canShow)
            return false;
        asked = Ask { false, folder, {}, patterns, std::move (chosen) };
        return true;
    }
    bool write (const juce::File& file, const std::string& text) override
    {
        if (! writable)
            return false;
        files[file.getFullPathName()] = text;
        return true;
    }
    std::optional<std::string> read (const juce::File& file, std::size_t maxBytes) override
    {
        readLimits.push_back (maxBytes);
        const auto it = files.find (file.getFullPathName());
        if (it == files.end())
            return std::nullopt;
        return it->second.substr (0, maxBytes);
    }

    /* The person chose `file` -- or cancelled. */
    void answer (const juce::File& file)
    {
        auto a = std::move (asked);
        asked.reset();
        if (a && a->chosen)
            a->chosen (file);
    }
    void cancel() { answer (juce::File()); }
};

/* A folder that reads as one on every platform, for answers. */
inline juce::File testFolder()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ni-tg-tests");
}

} // namespace ni::tg::test
