// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The built NIVocalize.vst3, hosted the way a DAW hosts it: JUCE's own VST3
 * host, the bundle from build/out, nothing of the plugin's sources.
 *
 *   vz_host <NIVocalize.vst3>
 *
 * WHAT ONLY THE BUNDLE CAN SHOW, after vz_processor has held the processor:
 *
 *   the class        one class, NI Vocalize from Neon Ingvy, filed as
 *                    Fx|Filter; an effect with a main bus in and out, no
 *                    MIDI either way
 *   the parameters   through the VST3 controller: the engine's ten by
 *                    name and the host's Bypass, their default texts, typing
 *                    read by the engine, Mode not offered for automation
 *                    and Ceiling offered
 *   the latency      Mode's, as the host learns it: none for Time, one frame
 *                    for Spectral, two at 96 kHz -- the host told through
 *                    the controller's restart, on the message thread
 *   the sound        both golden patches, typed into the controller as a
 *                    user would, through the VST3's audio path: the engine's
 *                    own reference renders, bit for bit (render_golden.h, as
 *                    vz_render_ab holds the engine). The Spectral patch
 *                    starts, as the reference does, on an engine made in
 *                    Time: the plugin's engine first hears the patch at its
 *                    first block, as vz_core_new hears it before its first
 *                    block, and both dip into Spectral over the same 20 ms
 *   the state        saved and reopened in a second instance, which then
 *                    renders the same
 *   the bypass       the host's Bypass passes the audio, untouched in Time,
 *                    and as late as the latency reported in Spectral
 *   the voice        a set holding a voice reopens with it, every formant as
 *                    written, and the bundle sings it: at Voice 100 not the
 *                    textbook's, at Voice 0 the textbook's bit for bit. A
 *                    capture itself needs the plugin's window; vz_processor
 *                    holds it
 *   the window       opened while it plays, at the design's size, closed and
 *                    opened again
 */
#include "render_golden.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

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

/* The engine's parameters by name, in its table's order (vz_param_name). */
const char* const names[VZ_RENDER_PARAMS_COUNT] = { "Mode", "Vocal", "Vowel", "Size", "Resonance", "Excite", "Mix", "Output", "Voice", "Ceiling" };
/* Mode's labels (vz_param_label): a choice is typed by its label. */
const char* const modes[] = { "Time", "Spectral" };
/* Ceiling's (vz_param_label). */
const char* const switches[] = { "Off", "On" };
/* The two choices' places in the table (vz_param_*). */
constexpr int modeIndex = 0, ceilingIndex = 9;

/* One frame of the Spectral engine at 44.1 and 48 kHz, and at 96 kHz. */
constexpr int frame = 1024;
constexpr int doubleFrame = 2048;

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

/* The message loop runs until `done`, or for two seconds at most: the
 * plugin tells its latency there, and the host takes it there. */
bool pumpUntil (const std::function<bool()>& done)
{
    const auto until = Time::getMillisecondCounter() + 2000;
    while (! done() && Time::getMillisecondCounter() < until)
        MessageManager::getInstance()->runDispatchLoopUntil (5);
    return done();
}

bool setMode (AudioPluginInstance& p, int mode)
{
    auto* param = byName (p, "Mode");
    if (param == nullptr)
        return false;
    param->setValue (param->getValueForText (modes[mode]));
    return true;
}

/* A patch, each value typed as text, as a user types it: Mode and Ceiling
 * by their labels, every number as it is written in render_golden.h. */
bool typePatch (AudioPluginInstance& p, const double* patch)
{
    for (int i = 0; i < VZ_RENDER_PARAMS_COUNT; ++i)
    {
        auto* param = byName (p, names[i]);
        if (param == nullptr)
            return false;
        const auto text = i == modeIndex      ? String (modes[(int) patch[i]])
                          : i == ceilingIndex ? String (switches[(int) patch[i]])
                                              : String (patch[i]);
        param->setValue (param->getValueForText (text));
    }
    return true;
}

/* Four seconds of the reference input; the hash of the left channel, from
 * frame `from` on. */
uint64 render (AudioPluginInstance& p, int from = 0)
{
    static float inL[VZ_RENDER_FRAMES], inR[VZ_RENDER_FRAMES];
    vz_render_input (inL, inR, VZ_RENDER_FRAMES);
    uint64 fnv = VZ_RENDER_FNV_START;
    AudioBuffer<float> buffer (2, VZ_RENDER_BLOCK);
    MidiBuffer midi;
    for (int done = 0; done < VZ_RENDER_FRAMES; done += VZ_RENDER_BLOCK)
    {
        const int n = jmin (VZ_RENDER_BLOCK, VZ_RENDER_FRAMES - done);
        buffer.setSize (2, n, false, false, true);
        buffer.copyFrom (0, 0, inL + done, n);
        buffer.copyFrom (1, 0, inR + done, n);
        p.processBlock (buffer, midi);
        if (done >= from)
            fnv = vz_render_fnv1a (fnv, buffer.getReadPointer (0), sizeof (float) * (size_t) n);
    }
    return fnv;
}

/* One click at the first sample, then silence, for `blocks` blocks of 512:
 * the left channel that came out. */
std::vector<float> click (AudioPluginInstance& p, int blocks)
{
    AudioBuffer<float> buffer (2, 512);
    MidiBuffer midi;
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        if (b == 0)
        {
            buffer.setSample (0, 0, 1.0f);
            buffer.setSample (1, 0, 1.0f);
        }
        p.processBlock (buffer, midi);
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 512);
    }
    return out;
}

/* Where the loudest sample of `out` is. */
int peakAt (const std::vector<float>& out)
{
    int at = 0;
    for (size_t i = 0; i < out.size(); ++i)
        if (std::abs (out[i]) > std::abs (out[(size_t) at]))
            at = (int) i;
    return at;
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
    check (sub == "Fx|Filter", "filed as Fx|Filter", sub);
    check (! d.isInstrument, "an effect, not an instrument");
}

void checkBuses (AudioPluginInstance& p)
{
    std::printf (" the buses\n");
    check (! p.acceptsMidi() && ! p.producesMidi(), "no MIDI either way");
    check (p.getBusCount (true) == 1 && p.getBusCount (false) == 1, "one input, one output",
           String (p.getBusCount (true)) + " in, " + String (p.getBusCount (false)) + " out");
}

void checkParameters (AudioPluginInstance& p)
{
    std::printf (" the parameters\n");
    int found = 0;
    for (const auto* name : names)
        found += byName (p, name) != nullptr ? 1 : 0;
    check (found == VZ_RENDER_PARAMS_COUNT, "the engine's ten, by name", String (found));
    check (p.getBypassParameter() != nullptr, "and the host's Bypass");
    if (found != VZ_RENDER_PARAMS_COUNT)
        return;
    const auto text = [&p] (const char* name) { return byName (p, name)->getCurrentValueAsText(); };
    check (text ("Mode") == "Time", "Mode starts at Time", text ("Mode"));
    check (text ("Vocal") == "80 %", "Vocal starts at 80 %", text ("Vocal"));
    check (text ("Vowel") == "A", "Vowel starts at A", text ("Vowel"));
    check (text ("Output") == "0.0 dB", "Output starts at 0.0 dB", text ("Output"));
    check (text ("Voice") == "100 %", "Voice starts at 100 %", text ("Voice"));
    check (byName (p, "Voice")->isAutomatable(), "Voice is offered for automation");
    check (! byName (p, "Mode")->isAutomatable(), "Mode is not offered for automation");
    check (byName (p, "Vowel")->isAutomatable(), "... and Vowel is");
    check (byName (p, "Mode")->getNumSteps() == 2, "Mode is a choice of two");
    check (text ("Ceiling") == "On", "Ceiling starts On", text ("Ceiling"));
    check (byName (p, "Ceiling")->getNumSteps() == 2, "Ceiling is a choice of two");
    check (byName (p, "Ceiling")->isAutomatable(), "Ceiling is offered for automation");

    auto* vowel = byName (p, "Vowel");
    const auto typed = vowel->getText (vowel->getValueForText ("i"), 32);
    check (typed == "I", "typing i reads as I", typed);
    const auto between = vowel->getText (vowel->getValueForText ("E 30"), 32);
    check (between == "E>I 30 %", "typing E 30 reads as E>I 30 %", between);
}

void checkLatency (VST3PluginFormat& format, const PluginDescription& d)
{
    std::printf (" the latency\n");
    auto p = load (format, d, 48000.0, 512);
    if (p == nullptr)
        return check (false, "the plugin loads");
    check (p->getLatencySamples() == 0, "none in Time", String (p->getLatencySamples()));

    check (setMode (*p, 1), "Mode set to Spectral");
    check (pumpUntil ([&p] { return p->getLatencySamples() == frame; }), "one frame in Spectral, 1024 samples",
           String (p->getLatencySamples()));
    check (setMode (*p, 0), "Mode set back to Time");
    check (pumpUntil ([&p] { return p->getLatencySamples() == 0; }), "... and none again",
           String (p->getLatencySamples()));

    /* A set at 96 kHz, in Spectral: the host prepares the plugin again. */
    setMode (*p, 1);
    p->releaseResources();
    p->prepareToPlay (96000.0, 512);
    check (pumpUntil ([&p] { return p->getLatencySamples() == doubleFrame; }), "two frames at 96 kHz, 2048 samples",
           String (p->getLatencySamples()));
}

void checkSound (VST3PluginFormat& format, const PluginDescription& d)
{
    std::printf (" the sound\n");
    const struct
    {
        const char* name;
        const double* patch;
        uint64 golden;
    } patches[] = { { "Time", VZ_RENDER_TIME, VZ_RENDER_GOLDEN_TIME },
                    { "Spectral", VZ_RENDER_SPECTRAL, VZ_RENDER_GOLDEN_SPECTRAL } };

    for (const auto& patch : patches)
    {
        auto p = load (format, d, VZ_RENDER_SR, VZ_RENDER_BLOCK);
        if (p == nullptr)
            return check (false, "the plugin loads");
        check (typePatch (*p, patch.patch), String (patch.name) + ": the patch typed into the controller");
        const auto fnv = render (*p);
        check (fnv == patch.golden, String (patch.name) + ": 4 s through the VST3 are the engine's reference render",
               hex (fnv) + " / " + hex (patch.golden));

        /* Its state, reopened in a second instance, renders the same. */
        MemoryBlock state;
        p->getStateInformation (state);
        auto q = load (format, d, VZ_RENDER_SR, VZ_RENDER_BLOCK);
        if (q == nullptr)
            return check (false, "a second instance loads");
        q->setStateInformation (state.getData(), (int) state.getSize());
        const auto vowel = byName (*q, "Vowel")->getCurrentValueAsText();
        check (vowel == "E>I 70 %", String (patch.name) + ": the state reopens: Vowel", vowel);
        const auto mode = byName (*q, "Mode")->getCurrentValueAsText();
        check (mode == patch.name, String (patch.name) + ": ... and Mode", mode);
        const auto again = render (*q);
        check (again == patch.golden, String (patch.name) + ": ... and renders the reference again", hex (again));
    }
}

void checkBypass (VST3PluginFormat& format, const PluginDescription& d)
{
    std::printf (" the bypass\n");
    for (const int mode : { 0, 1 })
    {
        auto p = load (format, d, 48000.0, 512);
        if (p == nullptr)
            return check (false, "the plugin loads");
        auto* bypass = p->getBypassParameter();
        if (bypass == nullptr)
            return check (false, "the host's Bypass");
        setMode (*p, mode);
        byName (*p, "Vocal")->setValue (1.0f);
        pumpUntil ([&p, mode] { return p->getLatencySamples() == (mode == 0 ? 0 : frame); });
        const int latency = p->getLatencySamples();
        /* A second, so a switch of engines has landed. */
        click (*p, 100);

        bypass->setValueNotifyingHost (1.0f);
        const auto out = click (*p, 4);
        const int at = peakAt (out);
        check (at == latency && juce::exactlyEqual (out[(size_t) at], 1.0f),
               String ("bypassed in ") + modes[mode] + ", the audio passes untouched, as late as the latency",
               "click at " + String (at) + ", latency " + String (latency));
        bypass->setValueNotifyingHost (0.0f);
    }
}

/* ------------------------------------------------------------ the voice -- */

/*
 * A VOICE IN A SET, THROUGH THE BUNDLE. A capture needs a person and a file,
 * so the voice here is written into a set by hand, where the plugin keeps it:
 * the host's state ("VST3PluginState") holds the component's state, base64,
 * which is the processor's own binary XML -- <voice> a child of its root --
 * followed by JUCE's private data, kept as it was.
 */

/* The textbook voice (model.rs) times 1.15: a bigger singer. */
constexpr double textbook[5][5] = {
    { 768.0, 1333.0, 2522.0, 3500.0, 4500.0 },
    { 476.0, 2089.0, 2691.0, 3500.0, 4500.0 },
    { 342.0, 2322.0, 3000.0, 3500.0, 4500.0 },
    { 497.0, 910.0, 2459.0, 3500.0, 4500.0 },
    { 378.0, 997.0, 2343.0, 3500.0, 4500.0 },
};
constexpr double bandwidths[5] = { 60.0, 90.0, 120.0, 200.0, 250.0 };
constexpr double handScale = 1.15;

std::unique_ptr<XmlElement> handMadeVoice()
{
    auto voice = std::make_unique<XmlElement> ("voice");
    voice->setAttribute ("format", 1);
    voice->setAttribute ("name", "Hand Made.wav");
    voice->setAttribute ("scale", handScale);
    const char* const letters[] = { "A", "E", "I", "O", "U" };
    for (int v = 0; v < 5; ++v)
    {
        auto* row = voice->createNewChildElement ("vowel");
        row->setAttribute ("name", letters[v]);
        row->setAttribute ("found", 1);
        row->setAttribute ("frames", 50);
        StringArray freq, width;
        for (int k = 0; k < 5; ++k)
        {
            freq.add (String (textbook[v][k] * handScale, 6));
            width.add (String (bandwidths[k], 6));
        }
        row->setAttribute ("freq", freq.joinIntoString (" "));
        row->setAttribute ("bandwidth", width.joinIntoString (" "));
    }
    return voice;
}

/* The component's state in a host's state: the processor's XML and what
 * follows it. */
struct ComponentState
{
    std::unique_ptr<XmlElement> host, processor;
    MemoryBlock rest;
};

ComponentState openState (const MemoryBlock& hostState)
{
    ComponentState s;
    s.host = AudioProcessor::getXmlFromBinary (hostState.getData(), (int) hostState.getSize());
    if (s.host == nullptr || s.host->getChildByName ("IComponent") == nullptr)
        return s;
    MemoryBlock component;
    if (! component.fromBase64Encoding (s.host->getChildByName ("IComponent")->getAllSubText()))
        return s;
    s.processor = AudioProcessor::getXmlFromBinary (component.getData(), (int) component.getSize());
    if (s.processor == nullptr || component.getSize() < 8)
        return s;
    /* JUCE's binary XML: magic, length, the text, a terminating zero. */
    const auto length = (size_t) ByteOrder::littleEndianInt (addBytesToPointer (component.getData(), 4));
    const size_t used = 9 + length;
    if (used <= component.getSize())
        s.rest.append (addBytesToPointer (component.getData(), (int) used), component.getSize() - used);
    return s;
}

MemoryBlock closeState (const ComponentState& s)
{
    MemoryBlock component;
    AudioProcessor::copyXmlToBinary (*s.processor, component);
    component.append (s.rest.getData(), s.rest.getSize());
    auto host = std::make_unique<XmlElement> (*s.host);
    auto* c = host->getChildByName ("IComponent");
    c->deleteAllTextElements();
    c->addTextElement (component.toBase64Encoding());
    MemoryBlock out;
    AudioProcessor::copyXmlToBinary (*host, out);
    return out;
}

void checkVoice (VST3PluginFormat& format, const PluginDescription& d)
{
    std::printf (" the voice\n");
    auto plain = load (format, d, VZ_RENDER_SR, VZ_RENDER_BLOCK);
    if (plain == nullptr)
        return check (false, "the plugin loads");
    MemoryBlock state;
    plain->getStateInformation (state);
    auto s = openState (state);
    check (s.processor != nullptr && s.processor->hasTagName ("NeonIngvy"), "the processor's state, inside the host's");
    if (s.processor == nullptr)
        return;
    check (s.processor->getChildByName ("voice") == nullptr, "no voice before one is captured");
    s.processor->addChildElement (handMadeVoice().release());
    const auto withVoice = closeState (s);

    /* Reopened, the set's voice comes back out as it went in. */
    auto sung = load (format, d, VZ_RENDER_SR, VZ_RENDER_BLOCK);
    if (sung == nullptr)
        return check (false, "a second instance loads");
    sung->setStateInformation (withVoice.getData(), (int) withVoice.getSize());
    MemoryBlock back;
    sung->getStateInformation (back);
    const auto reopened = openState (back);
    const auto* voice = reopened.processor != nullptr ? reopened.processor->getChildByName ("voice") : nullptr;
    check (voice != nullptr, "a set with a voice reopens with it");
    if (voice == nullptr)
        return;
    check (voice->getStringAttribute ("name") == "Hand Made.wav", "... by its name", voice->getStringAttribute ("name"));
    double worst = 0.0;
    int row = 0;
    for (const auto* vowel : voice->getChildWithTagNameIterator ("vowel"))
    {
        const auto freq = StringArray::fromTokens (vowel->getStringAttribute ("freq"), " ", "");
        for (int k = 0; k < jmin (5, freq.size()); ++k)
            worst = jmax (worst, std::abs (freq[k].getDoubleValue() - textbook[row][k] * handScale));
        ++row;
    }
    check (row == 5 && worst < 1.0e-6, "... every formant as it was written", String (worst));

    /* And the bundle sings it: the reference input through Vocal and Voice
     * all the way, against the textbook voice; at Voice 0 the same -- once
     * the glide from Voice's default 100 % has landed and died away in the
     * filters, so the last two seconds are compared. */
    const auto sing = [&] (AudioPluginInstance& p, const char* amount)
    {
        typePatch (p, VZ_RENDER_TIME);
        byName (p, "Vocal")->setValue (byName (p, "Vocal")->getValueForText ("100"));
        byName (p, "Voice")->setValue (byName (p, "Voice")->getValueForText (amount));
        return render (p, VZ_RENDER_FRAMES / 2);
    };
    auto textbookVoice = load (format, d, VZ_RENDER_SR, VZ_RENDER_BLOCK);
    auto again = load (format, d, VZ_RENDER_SR, VZ_RENDER_BLOCK);
    if (textbookVoice == nullptr || again == nullptr)
        return check (false, "more instances load");
    again->setStateInformation (withVoice.getData(), (int) withVoice.getSize());
    const auto heard = sing (*again, "100");
    const auto book = sing (*textbookVoice, "100");
    check (heard != book, "at Voice 100 the set's voice is sung, not the textbook's", hex (heard) + " / " + hex (book));

    auto quiet = load (format, d, VZ_RENDER_SR, VZ_RENDER_BLOCK);
    auto bookQuiet = load (format, d, VZ_RENDER_SR, VZ_RENDER_BLOCK);
    if (quiet == nullptr || bookQuiet == nullptr)
        return check (false, "more instances load");
    quiet->setStateInformation (withVoice.getData(), (int) withVoice.getSize());
    const auto none = sing (*quiet, "0");
    const auto bookNone = sing (*bookQuiet, "0");
    check (none == bookNone, "at Voice 0 the textbook's, bit for bit", hex (none) + " / " + hex (bookNone));
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
    /* Mode switched with the window open: the picture changes engine. */
    setMode (*p, 1);
    pump (50);
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
        std::fprintf (stderr, "usage: vz_host <NIVocalize.vst3>\n");
        return 2;
    }
    const ScopedJuceInitialiser_GUI gui;
    const File bundle (String::fromUTF8 (argv[1]));
    std::printf ("vz_host %s\n", bundle.getFullPathName().toRawUTF8());

    VST3PluginFormat format;
    OwnedArray<PluginDescription> found;
    format.findAllTypesForFile (found, bundle.getFullPathName());
    check (found.size() == 1, "one class in the bundle", String (found.size()));
    if (found.isEmpty())
        return 1;
    const auto& d = *found[0];
    check (d.name == "NI Vocalize" && d.manufacturerName == "Neon Ingvy", "NI Vocalize, from Neon Ingvy",
           d.name + " / " + d.manufacturerName);

    checkClass (bundle, d);
    if (auto p = load (format, d, 48000.0, 512))
    {
        checkBuses (*p);
        checkParameters (*p);
    }
    checkLatency (format, d);
    checkSound (format, d);
    checkBypass (format, d);
    checkVoice (format, d);
    checkWindow (format, d);

    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
