// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The built NIBitCrusher.vst3, hosted the way a DAW hosts it: JUCE's own VST3
 * host, the bundle from build/out, nothing of the plugin's sources.
 *
 *   bc_host <NIBitCrusher.vst3>
 *
 * WHAT ONLY THE BUNDLE CAN SHOW, after bc_processor has held the processor:
 *
 *   the class        one class, NI Bit-Crusher from Neon Ingvy, filed as
 *                    Fx|Distortion; an effect with a main bus in and out, no
 *                    MIDI either way, and its latency reported
 *   the parameters   through the VST3 controller: the engine's thirteen by
 *                    name, their default texts, typing read by the engine
 *   the sound        the golden patch, typed into the controller as a user
 *                    would, through the VST3's audio path: the engine's own
 *                    reference render, bit for bit (render_golden.h, as
 *                    bc_render_ab holds the engine) -- which is what says a
 *                    value on the table's resolution crosses the host's float
 *                    normalisation exactly
 *   the state        saved and reopened in a second instance, which then
 *                    renders the same
 *   the window       opened while it plays, at the design's size, closed and
 *                    opened again
 */
#include "render_golden.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <memory>

using namespace juce;

namespace
{
int failures = 0;

void check (bool ok, const String& what, const String& detail = {})
{
    std::printf ("  %-70s %s%s\n", what.toRawUTF8(), ok ? "ok" : "FAIL",
                 detail.isEmpty() ? "" : (" (" + detail + ")").toRawUTF8());
    if (! ok)
        ++failures;
}

/* The engine's parameters by name, in its table's order (bc_param_name). */
const char* const names[BC_RENDER_PARAMS_COUNT] = { "Drive", "Bits", "Shape", "Mid-Rise", "Dither", "Anti-Alias", "Rate",
                                                    "Jitter", "Pre-Filter", "Post-Filter", "Octave", "Mix", "Output",
                                                    "Refresh" };

std::unique_ptr<AudioPluginInstance> load (VST3PluginFormat& format, const PluginDescription& d, double rate, int block)
{
    String error;
    auto p = format.createInstanceFromDescription (d, rate, block, error);
    if (p != nullptr)
        p->prepareToPlay (rate, block);
    return p;
}

AudioProcessorParameter* byName (AudioPluginInstance& p, const String& name)
{
    for (auto* param : p.getParameters())
        if (param->getName (64) == name)
            return param;
    return nullptr;
}

/* The golden patch, each value typed as text, as a user types it. */
bool typePatch (AudioPluginInstance& p)
{
    for (int i = 0; i < BC_RENDER_PARAMS_COUNT; ++i)
    {
        auto* param = byName (p, names[i]);
        if (param == nullptr)
            return false;
        /* A number as its digits; a choice as its word ("1/s"), which is what
         * a user picks. */
        const bool choice = param->isDiscrete() && ! param->isBoolean() && param->getNumSteps() > 2;
        const auto text = choice ? param->getText ((float) BC_RENDER_PARAMS[i] / (float) (param->getNumSteps() - 1), 64)
                                 : String (BC_RENDER_PARAMS[i]);
        param->setValue (param->getValueForText (text));
    }
    return true;
}

/* Four seconds of the reference input; the hash of the left channel. */
uint64 render (AudioPluginInstance& p)
{
    static float inL[BC_RENDER_FRAMES], inR[BC_RENDER_FRAMES];
    bc_render_input (inL, inR, BC_RENDER_FRAMES);
    uint64 fnv = BC_RENDER_FNV_START;
    AudioBuffer<float> buffer (2, BC_RENDER_BLOCK);
    MidiBuffer midi;
    for (int done = 0; done < BC_RENDER_FRAMES; done += BC_RENDER_BLOCK)
    {
        const int n = jmin (BC_RENDER_BLOCK, BC_RENDER_FRAMES - done);
        buffer.setSize (2, n, false, false, true);
        buffer.copyFrom (0, 0, inL + done, n);
        buffer.copyFrom (1, 0, inR + done, n);
        p.processBlock (buffer, midi);
        fnv = bc_render_fnv1a (fnv, buffer.getReadPointer (0), sizeof (float) * (size_t) n);
    }
    return fnv;
}

String hex (uint64 v)
{
    return String::toHexString ((int64) v);
}

/* ---------------------------------------------------------------- checks -- */

void checkClass (const File& bundle, const PluginDescription& d)
{
    std::printf (" the class\n");
    const auto info = JSON::parse (bundle.getChildFile ("Contents/Resources/moduleinfo.json"));
    String sub;
    if (const auto* classes = info["Classes"].getArray())
        for (const auto& c : *classes)
            if (c["Category"].toString() == "Audio Module Class")
                for (const auto& s : *c["Sub Categories"].getArray())
                    sub << (sub.isEmpty() ? "" : "|") << s.toString();
    check (sub == "Fx|Distortion", "filed as Fx|Distortion", sub);
    check (! d.isInstrument, "an effect, not an instrument");
}

void checkBuses (AudioPluginInstance& p)
{
    std::printf (" the buses\n");
    check (! p.acceptsMidi() && ! p.producesMidi(), "no MIDI either way");
    check (p.getBusCount (true) == 1 && p.getBusCount (false) == 1, "one input, one output",
           String (p.getBusCount (true)) + " in, " + String (p.getBusCount (false)) + " out");
    check (p.getLatencySamples() == 32, "32 samples of latency, reported", String (p.getLatencySamples()));
}

void checkParameters (AudioPluginInstance& p)
{
    std::printf (" the parameters\n");
    int found = 0;
    for (const auto* name : names)
        found += byName (p, name) != nullptr ? 1 : 0;
    check (found == BC_RENDER_PARAMS_COUNT, "the engine's thirteen, by name", String (found));
    if (found != BC_RENDER_PARAMS_COUNT)
        return;
    check (byName (p, "Rate")->getCurrentValueAsText() == "12.0 kHz", "Rate starts at 12.0 kHz",
           byName (p, "Rate")->getCurrentValueAsText());
    check (byName (p, "Bits")->getCurrentValueAsText() == "8.0 bits", "Bits starts at 8.0 bits",
           byName (p, "Bits")->getCurrentValueAsText());
    check (byName (p, "Mid-Rise")->getNumSteps() == 2, "Mid-Rise is a switch");
    auto* rate = byName (p, "Rate");
    check (rate->getText (rate->getValueForText ("8k"), 32) == "8.00 kHz", "typing 8k reads as 8 kHz",
           rate->getText (rate->getValueForText ("8k"), 32));
}

void checkSound (VST3PluginFormat& format, const PluginDescription& d)
{
    std::printf (" the sound\n");
    auto p = load (format, d, BC_RENDER_SR, BC_RENDER_BLOCK);
    if (p == nullptr)
        return check (false, "the plugin loads");
    check (typePatch (*p), "the patch typed into the controller");
    const auto fnv = render (*p);
    check (fnv == BC_RENDER_GOLDEN, "4 s through the VST3 are the engine's reference render",
           hex (fnv) + " / " + hex (BC_RENDER_GOLDEN));

    /* Its state, reopened in a second instance, renders the same. */
    MemoryBlock state;
    p->getStateInformation (state);
    auto q = load (format, d, BC_RENDER_SR, BC_RENDER_BLOCK);
    if (q == nullptr)
        return check (false, "a second instance loads");
    q->setStateInformation (state.getData(), (int) state.getSize());
    check (byName (*q, "Bits")->getCurrentValueAsText() == "5.5 bits", "the state reopens: Bits",
           byName (*q, "Bits")->getCurrentValueAsText());
    check (render (*q) == BC_RENDER_GOLDEN, "... and renders the reference again");
}

void checkWindow (VST3PluginFormat& format, const PluginDescription& d)
{
    std::printf (" the window\n");
    auto p = load (format, d, 48000.0, 512);
    if (p == nullptr)
        return check (false, "the plugin loads");
    AudioBuffer<float> buffer (2, 512);
    MidiBuffer midi;
    const auto pump = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (c, i, 0.5f * std::sin (0.05f * (float) (b * 512 + i)));
            p->processBlock (buffer, midi);
            MessageManager::getInstance()->runDispatchLoopUntil (2);
        }
    };

    std::unique_ptr<AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    check (editor != nullptr, "the editor opens");
    if (editor == nullptr)
        return;
    pump (50);
    check (editor->getWidth() == 888 && editor->getHeight() == 532, "the window is the design's, 888 x 532",
           String (editor->getWidth()) + " x " + String (editor->getHeight()));
    editor.reset();
    pump (10);
    editor.reset (p->createEditorAndMakeActive());
    check (editor != nullptr, "the editor opens again");
    if (editor != nullptr)
    {
        pump (20);
        check (editor->getWidth() == 888 && editor->getHeight() == 532, "... at the same size");
    }
    editor.reset();
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf (stderr, "usage: bc_host <NIBitCrusher.vst3>\n");
        return 2;
    }
    const ScopedJuceInitialiser_GUI gui;
    const File bundle (String::fromUTF8 (argv[1]));
    std::printf ("bc_host %s\n", bundle.getFullPathName().toRawUTF8());

    VST3PluginFormat format;
    OwnedArray<PluginDescription> found;
    format.findAllTypesForFile (found, bundle.getFullPathName());
    check (found.size() == 1, "one class in the bundle", String (found.size()));
    if (found.isEmpty())
        return 1;
    const auto& d = *found[0];
    check (d.name == "NI Bit-Crusher" && d.manufacturerName == "Neon Ingvy", "NI Bit-Crusher, from Neon Ingvy",
           d.name + " / " + d.manufacturerName);

    checkClass (bundle, d);
    if (auto p = load (format, d, 48000.0, 512))
    {
        checkBuses (*p);
        checkParameters (*p);
    }
    checkSound (format, d);
    checkWindow (format, d);

    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
