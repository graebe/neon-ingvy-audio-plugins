// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A built single-filter bundle, as a DAW hosts it: what only the bundle can
 * show. One program for the nine, run on each bundle with its product's
 * name.
 *
 *   the class        one class, NI <Name> from Neon Ingvy, filed as a
 *                    filter or an EQ effect, no latency
 *   the parameters   through the VST3 controller: the product's own and the
 *                    host's Bypass; typing read by the engine
 *   the sound        heard through the VST3's audio path, as the product
 *                    does it: a cut cuts, a bell boosts, an all-pass keeps
 *                    the level
 *   the tail         what the host asks for: the engine's
 *   the state        saved and reopened in a second instance, which then
 *                    sounds the same
 *   the window       opens at the design's size, 664 x 572, and again
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

/* Each product: its parameter count, its category, and what it is heard to
 * do -- the settings typed in, the tone, and the gain to expect. */
struct Expect
{
    const char* name;
    int count;
    const char* category;
    std::initializer_list<std::pair<const char*, const char*>> settings;
    double hz, db, tolerance;
};

const Expect expectations[] = {
    { "NI High Pass", 10, "Filter", { { "Freq", "1000" }, { "Slope", "24 dB/oct" } }, 250.0, -48.0, 1.0 },
    { "NI Low Pass", 10, "Filter", { { "Freq", "1000" }, { "Slope", "24 dB/oct" } }, 4000.0, -48.0, 1.0 },
    { "NI Band Pass", 9, "Filter", { { "Freq", "1000" }, { "Q", "4" } }, 1000.0, 0.0, 0.1 },
    { "NI Notch", 9, "Filter", { { "Freq", "1000" }, { "Q", "2" } }, 1000.0, -60.0, 20.0 },
    { "NI Bell", 7, "EQ", { { "Gain", "6" } }, 1000.0, 6.0, 0.1 },
    { "NI Low Shelf", 7, "EQ", { { "Gain", "-6" } }, 40.0, -6.0, 0.2 },
    { "NI High Shelf", 7, "EQ", { { "Gain", "6" } }, 18000.0, 6.0, 0.3 },
    { "NI Tilt", 5, "EQ", { { "Gain", "6" } }, 1000.0, 0.0, 0.1 },
    { "NI All Pass", 6, "Filter", { { "Order", "4th order" } }, 1000.0, 0.0, 0.05 },
};
} // namespace

int main (int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf (stderr, "usage: sf_host <NI<Name>.vst3> \"NI <Name>\"\n");
        return 2;
    }
    const ScopedJuceInitialiser_GUI gui;
    const File bundle (String::fromUTF8 (argv[1]));
    const String want = String::fromUTF8 (argv[2]);
    std::printf ("sf_host %s\n", bundle.getFullPathName().toRawUTF8());
    const Expect* e = nullptr;
    for (const auto& x : expectations)
        if (want == x.name)
            e = &x;
    if (e == nullptr)
    {
        std::fprintf (stderr, "sf_host: no product named %s\n", argv[2]);
        return 2;
    }

    VST3PluginFormat format;
    OwnedArray<PluginDescription> found;
    format.findAllTypesForFile (found, bundle.getFullPathName());
    check (found.size() == 1, "one class in the bundle", String (found.size()));
    if (found.isEmpty())
        return 1;
    const auto& d = *found[0];
    check (d.name == want && d.manufacturerName == "Neon Ingvy", want + ", from Neon Ingvy", d.name + " / " + d.manufacturerName);
    check (d.category.contains (e->category), String ("filed as ") + e->category, d.category);
    check (! d.isInstrument, "an effect");

    auto p = load (format, d, 48000.0, 512);
    if (p == nullptr)
    {
        check (false, "the plugin loads");
        return 1;
    }
    check (p->getLatencySamples() == 0, "no latency");
    check (p->getParameters().size() == e->count + 1, "its parameters and the Bypass", String (p->getParameters().size()));
    check (p->getBypassParameter() != nullptr && p->getBypassParameter()->getName (64) == "Bypass",
           "the plugin's Bypass is the host's bypass");
    if (auto* f = byName (*p, "Freq"))
    {
        f->setValue (f->getValueForText ("2.5k"));
        check (f->getText (f->getValue(), 64) == "2.50 kHz", "typing is the engine's: 2.5k is 2.50 kHz", f->getText (f->getValue(), 64));
    }
    else
        check (false, "a Freq");

    type (*p, "Freq", "1000");
    for (const auto& [name, text] : e->settings)
        type (*p, name, text);
    const double g = gainAt (*p, e->hz);
    check (std::abs (g - e->db) < e->tolerance || (e->db <= -60.0 && g < -40.0),
           "heard as it is named: " + String (e->db, 1) + " dB at " + String (e->hz, 0) + " Hz", String (g, 3));
    check (p->getTailLengthSeconds() > 0.0 && p->getTailLengthSeconds() < 1.0, "a tail, from the engine",
           String (p->getTailLengthSeconds(), 4));

    MemoryBlock state;
    p->getStateInformation (state);
    auto q = load (format, d, 48000.0, 512);
    q->setStateInformation (state.getData(), (int) state.getSize());
    const double g2 = gainAt (*q, e->hz);
    check (std::abs (g2 - g) < 0.01, "... and so in an instance opened from its state", String (g2, 3));

    std::unique_ptr<AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    check (editor != nullptr, "the editor opens");
    if (editor != nullptr)
    {
        for (int k = 0; k < 20; ++k)
            MessageManager::getInstance()->runDispatchLoopUntil (5);
        check (editor->getWidth() == 664 && editor->getHeight() == 572, "the window is the design's, 664 x 572",
               String (editor->getWidth()) + " x " + String (editor->getHeight()));
        editor.reset();
        editor.reset (p->createEditorAndMakeActive());
        check (editor != nullptr, "the editor opens again");
        editor.reset();
    }

    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
