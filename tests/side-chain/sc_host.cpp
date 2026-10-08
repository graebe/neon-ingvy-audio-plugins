// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The built NISideChain.vst3, hosted the way a DAW hosts it: JUCE's own VST3
 * host, the bundle from build/out, nothing of the plugin's sources.
 *
 *   sc_host <NISideChain.vst3> <tests/fixtures/iplug2>
 *
 * WHAT ONLY THE BUNDLE CAN SHOW, after sc_processor has held the processor:
 *
 *   the class        the iPlug2 build's class ID (ids.json), as the factory and
 *                    moduleinfo.json report it, and no compatibility entry;
 *                    Fx|Dynamics, MIDI in, a main bus and a sidechain
 *   the parameters   through the VST3 controller: the iPlug2 build's IDs,
 *                    names, units, steps, defaults and default texts; Bypass
 *                    the host's, at ID 15; and none at the iPlug2 MIDI-CC
 *                    parameters' IDs, which this build does not have
 *   every fixture    loaded as Live reopens a set -- component state, then
 *                    controller state -- restores every value and display the
 *                    iPlug2 build reported, and the component state saved back
 *                    is the fixture's chunk byte for byte
 *   the sound        the golden patch through the VST3's audio path: the
 *                    engine's own reference render, bit for bit
 *                    (render_golden.h, as sc_render_ab holds the engine)
 *   the MIDI         a note through the VST3's event list holds a Gate duck,
 *                    and CC 123 and CC 120 -- through the host's MIDI-CC
 *                    mapping, as Live sends them -- open it: the panic; the
 *                    host's Bypass passes the audio through untouched, the
 *                    engine still hearing the notes released meanwhile
 *   the key          the sidechain bus, connected by the host, ducks the track
 *   the window       opened while the transport runs, at the design's size, a
 *                    set loaded on another thread while it is open, closed and
 *                    opened again
 */
#include "render_golden.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <thread>

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

/* A transport playing from bar 1 at 120 BPM, its position accumulated block
 * by block as render_ab.c computes it, so both renders see the same beats. */
struct Transport final : AudioPlayHead
{
    double rate = 48000.0;
    int64 sample = 0;

    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true);
        p.setBpm (SC_RENDER_BPM);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setTimeInSamples (sample);
        p.setPpqPosition ((double) sample / rate * (SC_RENDER_BPM / 60.0));
        return p;
    }
};

std::unique_ptr<AudioPluginInstance> load (VST3PluginFormat& format, const PluginDescription& d, double rate, int block,
                                           bool keyed = false)
{
    String error;
    auto p = format.createInstanceFromDescription (d, rate, block, error);
    if (p == nullptr)
        return p;
    if (keyed && p->getBusCount (true) > 1)
    {
        auto layout = p->getBusesLayout();
        layout.inputBuses.getReference (1) = AudioChannelSet::stereo();
        p->setBusesLayout (layout);
    }
    p->prepareToPlay (rate, block);
    return p;
}

/* A hosted parameter by its VST3 ID. */
AudioProcessorParameter* byId (AudioPluginInstance& p, const String& id)
{
    for (auto* param : p.getParameters())
        if (auto* hosted = dynamic_cast<HostedAudioProcessorParameter*> (param))
            if (hosted->getParameterID() == id)
                return param;
    return nullptr;
}

/* This build's counterpart of an iPlug2 parameter ID: the same, but Bypass. */
AudioProcessorParameter* counterpart (AudioPluginInstance& p, const String& id)
{
    return id == "65536" ? p.getBypassParameter() : byId (p, id);
}

/* What a VST3 host keeps of an instance, in the form JUCE's host reads back. */
MemoryBlock hostState (const MemoryBlock& component, const MemoryBlock& controller)
{
    XmlElement state ("VST3PluginState");
    state.createNewChildElement ("IComponent")->addTextElement (component.toBase64Encoding());
    state.createNewChildElement ("IEditController")->addTextElement (controller.toBase64Encoding());
    MemoryBlock out;
    AudioProcessor::copyXmlToBinary (state, out);
    return out;
}

/*
 * IComponent::getState's chunk, from what JUCE's host saved, with the JUCE
 * wrapper's private trailer -- 16 bytes and "JUCEPrivateData" after the chunk,
 * which the wrapper strips again before the plugin reads -- taken off.
 */
MemoryBlock componentChunk (AudioPluginInstance& p)
{
    MemoryBlock saved, component;
    p.getStateInformation (saved);
    if (auto xml = AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize()))
        if (auto* c = xml->getChildByName ("IComponent"))
            component.fromBase64Encoding (c->getAllSubText());
    const char* magic = "JUCEPrivateData";
    const auto n = std::strlen (magic);
    auto size = component.getSize();
    const auto* bytes = static_cast<const char*> (component.getData());
    if (size >= n + 16 && std::memcmp (bytes + size - n, magic, n) == 0)
    {
        uint64 priv;
        std::memcpy (&priv, bytes + size - n - 8, 8);
        size -= n + 16 + (size_t) priv;
    }
    return MemoryBlock (bytes, size);
}

MemoryBlock fileBytes (const File& f)
{
    MemoryBlock b;
    f.loadFileAsData (b);
    return b;
}

void putF64 (MemoryBlock& out, double v) { out.append (&v, 8); }
void putI32 (MemoryBlock& out, int32 v) { out.append (&v, 4); }

/* A set holding these fifteen plain values: the NIst chunk FORMAT.md gives,
 * as the host would hand it back. */
MemoryBlock setOf (const double (&params)[15])
{
    MemoryBlock body;
    for (const double v : params)
        putF64 (body, v);
    MemoryBlock chunk;
    const uint8 magic[8] { 'N', 'I', 's', 't', 0x00, 0x00, 0xF8, 0x7F };
    chunk.append (magic, 8);
    putI32 (chunk, 1);
    putI32 (chunk, (int32) body.getSize());
    chunk.append (body.getData(), body.getSize());
    putI32 (chunk, 0);
    return hostState (chunk, {});
}

void apply (AudioPluginInstance& p, const MemoryBlock& state)
{
    p.setStateInformation (state.getData(), (int) state.getSize());
}

/* ---------------------------------------------------------------- checks -- */

void checkClass (const File& bundle, const File& fixtures, const PluginDescription& d)
{
    std::printf (" the class\n");
    const auto ids = JSON::parse (fixtures.getChildFile ("ids.json"));
    const auto info = JSON::parse (bundle.getChildFile ("Contents/Resources/moduleinfo.json"));
    const auto iplug2 = ids["NISideChain"]["cid"].toString();
    String component, sub;
    for (const auto& c : *info["Classes"].getArray())
        if (c["Category"].toString() == "Audio Module Class")
        {
            component = c["CID"].toString();
            for (const auto& s : *c["Sub Categories"].getArray())
                sub << (sub.isEmpty() ? "" : "|") << s.toString();
        }
    check (component == iplug2, "the component class is the iPlug2 build's", component + " / " + iplug2);
    const auto* compat = info["Compatibility"].getArray();
    check (compat == nullptr || compat->isEmpty(), "moduleinfo.json declares no compatibility");
    check (sub == ids["NISideChain"]["subCategories"].toString(), "Fx|Dynamics, as the iPlug2 build was filed", sub);
    check (! d.isInstrument, "an effect, not an instrument");
}

void checkBuses (AudioPluginInstance& p)
{
    std::printf (" the buses\n");
    check (p.acceptsMidi(), "MIDI in");
    check (! p.producesMidi(), "no MIDI out");
    check (p.getBusCount (true) == 2 && p.getBusCount (false) == 1, "a main input and a sidechain, one output",
           String (p.getBusCount (true)) + " in, " + String (p.getBusCount (false)) + " out");
    if (p.getBusCount (true) == 2)
        check (p.getBus (true, 1)->getName() == "Sidechain", "the second input is the sidechain",
               p.getBus (true, 1)->getName());
}

void checkParameters (AudioPluginInstance& p, const File& dir)
{
    std::printf (" the parameters\n");
    const auto doc = JSON::parse (dir.getChildFile ("parameters.json"));
    const auto defaults = JSON::parse (dir.getChildFile ("default.json"));
    std::map<String, String> text;
    for (const auto& v : *defaults["values"].getArray())
        text[v["id"].toString()] = v["display"].toString();
    int count = 0, iplug2Midi = 0;
    for (const auto& row : *doc["parameters"].getArray())
    {
        const auto id = row["id"].toString();
        if ((int) row["id"] >= 65538)
        {
            /* iPlug2's MIDI-CC parameters: JUCE maps CCs onto its own. */
            iplug2Midi += byId (p, id) != nullptr ? 1 : 0;
            continue;
        }
        auto* param = counterpart (p, id);
        if (param == nullptr)
        {
            check (false, "parameter " + id + " exists");
            continue;
        }
        ++count;
        const int steps = param->isDiscrete() ? param->getNumSteps() - 1 : 0;
        const auto want = row["title"].toString() + " [" + row["units"].toString() + "] " + row["stepCount"].toString()
                        + " steps, default " + String ((double) row["defaultNormalizedValue"], 6) + " \"" + text[id] + "\"";
        const auto got = param->getName (128) + " [" + param->getLabel() + "] " + String (steps) + " steps, default "
                       + String (param->getDefaultValue(), 6) + " \"" + param->getText (param->getDefaultValue(), 128) + "\"";
        check (want == got, "parameter " + id + ": " + row["title"].toString(), want == got ? String() : got + " != " + want);
    }
    check (count == 16, "all fifteen and Bypass", String (count));
    auto* bypass = dynamic_cast<HostedAudioProcessorParameter*> (p.getBypassParameter());
    check (bypass != nullptr && bypass->getParameterID() == "15", "Bypass is the host's, at ID 15");
    check (iplug2Midi == 0, "no parameter at the iPlug2 build's MIDI-CC IDs (65538-65667)", String (iplug2Midi));
}

void checkFixture (VST3PluginFormat& format, const PluginDescription& d, const File& dir, const String& scenario)
{
    std::printf (" %s\n", scenario.toRawUTF8());
    const auto doc = JSON::parse (dir.getChildFile (scenario + ".json"));
    const auto component = fileBytes (dir.getChildFile (scenario + ".component.bin"));
    const auto controller = fileBytes (dir.getChildFile (scenario + ".controller.bin"));
    auto p = load (format, d, 48000.0, 512);
    if (p == nullptr)
        return check (false, "the plugin loads");
    apply (*p, hostState (component, controller));

    String wrong;
    for (const auto& v : *doc["values"].getArray())
    {
        const auto id = v["id"].toString();
        auto* param = counterpart (*p, id);
        if (param == nullptr || std::abs (param->getValue() - (double) v["normalized"]) > 1.0e-6
            || param->getText (param->getValue(), 128) != v["display"].toString())
            wrong << " " << id << "=" << (param ? param->getText (param->getValue(), 128) : String ("?")) << "!="
                  << v["display"].toString();
    }
    check (wrong.isEmpty(), "every value and display the iPlug2 build reported", wrong.trim());
    check (componentChunk (*p) == component, "the state saved back is the iPlug2 chunk, byte for byte");
}

void checkSound (VST3PluginFormat& format, const PluginDescription& d)
{
    std::printf (" the sound\n");
    auto p = load (format, d, SC_RENDER_SR, SC_RENDER_BLOCK);
    if (p == nullptr)
        return check (false, "the plugin loads");
    apply (*p, setOf (SC_RENDER_PARAMS));
    Transport transport;
    transport.rate = SC_RENDER_SR;
    p->setPlayHead (&transport);

    static float inL[SC_RENDER_FRAMES], inR[SC_RENDER_FRAMES];
    sc_render_input (inL, inR, SC_RENDER_FRAMES);
    uint64 fnv = SC_RENDER_FNV_START;
    AudioBuffer<float> buffer (2, SC_RENDER_BLOCK);
    MidiBuffer midi;
    for (int done = 0; done < SC_RENDER_FRAMES; done += SC_RENDER_BLOCK)
    {
        const int n = jmin (SC_RENDER_BLOCK, SC_RENDER_FRAMES - done);
        buffer.setSize (2, n, false, false, true);
        buffer.copyFrom (0, 0, inL + done, n);
        buffer.copyFrom (1, 0, inR + done, n);
        p->processBlock (buffer, midi);
        fnv = sc_render_fnv1a (fnv, buffer.getReadPointer (0), sizeof (float) * (size_t) n);
        transport.sample += n;
    }
    p->setPlayHead (nullptr);
    check (fnv == SC_RENDER_GOLDEN, "4 s through the VST3 are the engine's reference render",
           String::toHexString ((int64) fnv) + " / " + String::toHexString ((int64) SC_RENDER_GOLDEN));
}

/* Blocks of a steady 0.5 under the transport; the lowest and highest output. */
struct Run
{
    AudioPluginInstance& p;
    Transport transport;
    AudioBuffer<float> buffer;
    float lo = 1.0f, hi = 0.0f;

    Run (AudioPluginInstance& plugin) : p (plugin), buffer (jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), 512)
    {
        p.setPlayHead (&transport);
    }
    ~Run() { p.setPlayHead (nullptr); }

    void blocks (int count, MidiBuffer midi = {}, const std::function<float (int64)>& key = {})
    {
        lo = 1.0f;
        hi = 0.0f;
        for (int b = 0; b < count; ++b)
        {
            buffer.clear();
            for (int ch = 0; ch < 2; ++ch)
                FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.5f, 512);
            if (key)
                for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < 512; ++i)
                        buffer.setSample (ch, i, key (transport.sample + i));
            p.processBlock (buffer, midi);
            midi.clear();
            lo = jmin (lo, FloatVectorOperations::findMinimum (buffer.getReadPointer (0), 512));
            hi = jmax (hi, FloatVectorOperations::findMaximum (buffer.getReadPointer (0), 512));
            transport.sample += 512;
        }
    }
};

MidiBuffer one (const MidiMessage& m)
{
    MidiBuffer b;
    b.addEvent (m, 0);
    return b;
}

void checkMidi (VST3PluginFormat& format, const PluginDescription& d)
{
    std::printf (" the MIDI\n");
    /* MIDI, Gate, the rest at their defaults: note C1 on channel 1. */
    const double gate[15] { 1, 4, 0, 0, 2, 8, 35, 100, 1, 1, 36, 1, 0, -24, 20 };
    for (const int cc : { 123, 120 })
    {
        auto p = load (format, d, 48000.0, 512);
        if (p == nullptr)
            return check (false, "the plugin loads");
        apply (*p, setOf (gate));
        Run run (*p);
        run.blocks (4);
        check (run.lo == 0.5f, "nothing ducks before the trigger note");
        run.blocks (20, one (MidiMessage::noteOn (1, 36, (uint8) 100)));
        run.blocks (4);
        check (run.hi < 0.05f, "a held trigger note holds the duck", String (run.hi));
        run.blocks (1, one (MidiMessage::controllerEvent (1, cc, 0)));
        run.blocks (1);
        check (run.lo > 0.45f, "CC " + String (cc) + " through the host's MIDI-CC mapping opens it: the panic",
               String (run.lo));
    }

    auto p = load (format, d, 48000.0, 512);
    if (p == nullptr)
        return;
    Run run (*p);
    auto* bypass = p->getBypassParameter();
    bypass->setValueNotifyingHost (1.0f);
    run.blocks (60);
    check (run.lo == 0.5f && run.hi == 0.5f, "the host's Bypass passes the audio through untouched",
           String (run.lo) + " .. " + String (run.hi));
    bypass->setValueNotifyingHost (0.0f);
    run.blocks (60);
    check (run.lo < 0.05f, "... and back in, Cycle ducks", String (run.lo));

    /* Bypassed, the engine still hears the MIDI: a Gate note held, the
     * Bypass on, the note released while bypassed -- and with the Bypass off
     * again the duck is not held down by a note that is no longer there. */
    auto q = load (format, d, 48000.0, 512);
    if (q == nullptr)
        return;
    apply (*q, setOf (gate));
    Run held (*q);
    held.blocks (20, one (MidiMessage::noteOn (1, 36, (uint8) 100)));
    held.blocks (4);
    check (held.hi < 0.05f, "a held trigger note holds the duck", String (held.hi));
    q->getBypassParameter()->setValueNotifyingHost (1.0f);
    held.blocks (2);
    held.blocks (1, one (MidiMessage::noteOff (1, 36)));
    held.blocks (60);
    check (held.lo == 0.5f && held.hi == 0.5f, "bypassed, the audio passes untouched while the note is released",
           String (held.lo) + " .. " + String (held.hi));
    q->getBypassParameter()->setValueNotifyingHost (0.0f);
    held.blocks (2);
    held.blocks (4);
    check (held.lo > 0.45f, "... and back in, the note released while bypassed holds no duck", String (held.lo));
}

void checkKey (VST3PluginFormat& format, const PluginDescription& d)
{
    std::printf (" the key\n");
    const double keyed[15] { 2, 4, 0, 0, 2, 8, 35, 100, 1, 1, 36, 0, 0, -24, 20 };
    auto p = load (format, d, 48000.0, 512, true);
    if (p == nullptr)
        return check (false, "the plugin loads");
    check (p->getChannelCountOfBus (true, 1) == 2, "the host connects a stereo key",
           String (p->getChannelCountOfBus (true, 1)));
    apply (*p, setOf (keyed));
    Run run (*p);
    run.blocks (60, {}, [] (int64 s) { return s % 9600 < 480 ? 0.8f : 0.0f; });
    check (run.lo < 0.05f && run.hi > 0.45f, "a kick on the key ducks the track", String (run.lo) + " .. " + String (run.hi));
    /* The last kick's duck released, a silent key fires nothing more. */
    run.blocks (30);
    run.blocks (60);
    check (run.lo == 0.5f, "a silent key ducks nothing", String (run.lo));
}

void checkWindow (VST3PluginFormat& format, const PluginDescription& d, const File& dir)
{
    std::printf (" the window\n");
    auto p = load (format, d, 48000.0, 512);
    if (p == nullptr)
        return check (false, "the plugin loads");
    Run run (*p);
    const auto pump = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            run.blocks (1);
            MessageManager::getInstance()->runDispatchLoopUntil (2);
        }
    };

    std::unique_ptr<AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    check (editor != nullptr, "the editor opens");
    if (editor == nullptr)
        return;
    pump (50);
    check (editor->getWidth() == 760 && editor->getHeight() == 604, "the window is the design's, 760 x 604",
           String (editor->getWidth()) + " x " + String (editor->getHeight()));

    /* A set loaded on the host's own thread, the window open and the
     * transport running. */
    const auto custom = hostState (fileBytes (dir.getChildFile ("custom.component.bin")),
                                   fileBytes (dir.getChildFile ("custom.controller.bin")));
    std::thread host ([&] { apply (*p, custom); });
    pump (10);
    host.join();
    pump (50);
    check (std::abs (byId (*p, "7")->getValue() - 0.75f) < 1.0e-5f, "a set loaded on another thread reached the host");

    editor.reset();
    pump (10);
    editor.reset (p->createEditorAndMakeActive());
    check (editor != nullptr, "the editor opens again");
    if (editor != nullptr)
    {
        pump (20);
        check (editor->getWidth() == 760 && editor->getHeight() == 604, "... at the same size");
    }
    editor.reset();
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf (stderr, "usage: sc_host <NISideChain.vst3> <fixtures-dir>\n");
        return 2;
    }
    const ScopedJuceInitialiser_GUI gui;
    const File bundle (String::fromUTF8 (argv[1]));
    const File fixtures (String::fromUTF8 (argv[2]));
    const auto dir = fixtures.getChildFile ("NISideChain");
    std::printf ("sc_host %s\n", bundle.getFullPathName().toRawUTF8());

    VST3PluginFormat format;
    OwnedArray<PluginDescription> found;
    format.findAllTypesForFile (found, bundle.getFullPathName());
    check (found.size() == 1, "one class in the bundle", String (found.size()));
    if (found.isEmpty())
        return 1;
    const auto& d = *found[0];
    check (d.name == "NI Side-Chain" && d.manufacturerName == "Neon Ingvy", "NI Side-Chain, from Neon Ingvy",
           d.name + " / " + d.manufacturerName);

    checkClass (bundle, fixtures, d);
    if (auto p = load (format, d, 48000.0, 512))
    {
        checkBuses (*p);
        checkParameters (*p, dir);
    }
    for (const char* scenario : { "default", "custom" })
        checkFixture (format, d, dir, scenario);
    checkSound (format, d);
    checkMidi (format, d);
    checkKey (format, d);
    checkWindow (format, d, dir);

    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
