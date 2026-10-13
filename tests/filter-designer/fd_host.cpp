// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The built NIFilterDesigner.vst3, as a DAW hosts it: what only the bundle
 * can show.
 *
 *   the class        one class, NI Filter Designer from Neon Ingvy, filed as
 *                    an EQ effect, no latency
 *   the parameters   through the VST3 controller: the engine's 174 and the
 *                    host's Bypass; typing read by the engine
 *   the sound        a new instance is the input bit for bit; a Bell typed
 *                    into the controller is heard at its gain, through the
 *                    VST3's audio path
 *   the state        saved and reopened in a second instance, which then
 *                    sounds the same
 *   the window       opens at the design's size, 888 x 708, and again
 */
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>

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

void type (AudioPluginInstance& p, const String& name, const String& text)
{
    if (auto* param = byName (p, name))
        param->setValue (param->getValueForText (text));
    else
        check (false, "a parameter named " + name);
}

/* `blocks` of a sine at `hz`; the last block's gain in dB. */
double gainAt (AudioPluginInstance& p, double hz, int blocks = 60)
{
    AudioBuffer<float> buffer (2, 512);
    MidiBuffer midi;
    double in = 0.0, out = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (c, i, 0.25f * (float) std::sin (2.0 * MathConstants<double>::pi * hz * (b * 512 + i) / 48000.0));
        in = buffer.getRMSLevel (0, 0, 512);
        p.processBlock (buffer, midi);
        out = buffer.getRMSLevel (0, 0, 512);
    }
    return 20.0 * std::log10 (out / in);
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf (stderr, "usage: fd_host <NIFilterDesigner.vst3>\n");
        return 2;
    }
    const ScopedJuceInitialiser_GUI gui;
    const File bundle (String::fromUTF8 (argv[1]));
    std::printf ("fd_host %s\n", bundle.getFullPathName().toRawUTF8());

    VST3PluginFormat format;
    OwnedArray<PluginDescription> found;
    format.findAllTypesForFile (found, bundle.getFullPathName());
    check (found.size() == 1, "one class in the bundle", String (found.size()));
    if (found.isEmpty())
        return 1;
    const auto& d = *found[0];
    check (d.name == "NI Filter Designer" && d.manufacturerName == "Neon Ingvy", "NI Filter Designer, from Neon Ingvy",
           d.name + " / " + d.manufacturerName);
    check (d.category.contains ("EQ"), "filed as an EQ", d.category);
    check (! d.isInstrument, "an effect");

    auto p = load (format, d, 48000.0, 512);
    if (p == nullptr)
    {
        check (false, "the plugin loads");
        return 1;
    }
    check (p->getLatencySamples() == 0, "no latency");
    check (p->getParameters().size() == 175, "174 parameters and the Bypass", String (p->getParameters().size()));
    check (p->getBypassParameter() != nullptr && p->getBypassParameter()->getName (64) == "Bypass",
           "the plugin's Bypass is the host's bypass");
    check (byName (*p, "Filter 16 Channel") != nullptr, "the last node's Channel, by name");
    if (auto* f = byName (*p, "Filter 1 Freq"))
    {
        f->setValue (f->getValueForText ("2.5k"));
        check (f->getText (f->getValue(), 64) == "2.50 kHz", "typing is the engine's: 2.5k is 2.50 kHz", f->getText (f->getValue(), 64));
    }

    /* A new instance is the input, bit for bit. */
    {
        auto fresh = load (format, d, 48000.0, 512);
        AudioBuffer<float> buffer (2, 512), before (2, 512);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (c, i, 0.4f * std::sin (0.07f * (float) i + (float) c));
        before.makeCopyOf (buffer);
        MidiBuffer midi;
        fresh->processBlock (buffer, midi);
        bool same = true;
        for (int c = 0; c < 2; ++c)
            same = same && std::memcmp (buffer.getReadPointer (c), before.getReadPointer (c), 512 * sizeof (float)) == 0;
        check (same, "a new instance is the input, bit for bit");
    }

    /* A Bell typed in, heard at its gain. */
    type (*p, "Filter 1 Type", "Bell");
    type (*p, "Filter 2 Type", "High-Pass");
    type (*p, "Filter 2 Freq", "20");
    type (*p, "Filter 1 Freq", "1000");
    type (*p, "Filter 1 Gain", "6");
    const double g = gainAt (*p, 1000.0);
    check (std::abs (g - 6.0) < 0.1, "a Bell of +6 dB at 1 kHz is heard at +6 dB", String (g, 3));

    /* The state, in a second instance. */
    MemoryBlock state;
    p->getStateInformation (state);
    auto q = load (format, d, 48000.0, 512);
    q->setStateInformation (state.getData(), (int) state.getSize());
    const double g2 = gainAt (*q, 1000.0);
    check (std::abs (g2 - 6.0) < 0.1, "... and so in an instance opened from its state", String (g2, 3));

    /* The window. */
    std::unique_ptr<AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    check (editor != nullptr, "the editor opens");
    if (editor != nullptr)
    {
        for (int k = 0; k < 20; ++k)
            MessageManager::getInstance()->runDispatchLoopUntil (5);
        check (editor->getWidth() == 888 && editor->getHeight() == 708, "the window is the design's, 888 x 708",
               String (editor->getWidth()) + " x " + String (editor->getHeight()));
        editor.reset();
        editor.reset (p->createEditorAndMakeActive());
        check (editor != nullptr, "the editor opens again");
        editor.reset();
    }

    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
