// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The built NITranceGate.vst3, hosted the way a DAW hosts it: JUCE's own VST3
 * host, the bundle from build/out, nothing of the plugin's sources.
 *
 *   tg_host <NITranceGate.vst3> <tests/fixtures/iplug2>
 *
 * WHAT ONLY THE BUNDLE CAN SHOW, after tg_processor has held the processor:
 *
 *   the class        the iPlug2 build's class ID (ids.json), as the factory and
 *                    moduleinfo.json report it, and no compatibility entry
 *   the parameters   through the VST3 controller: the iPlug2 build's IDs,
 *                    names, units, steps, defaults and default texts; Bypass
 *                    the host's, at ID 15
 *   every fixture    loaded as Live reopens a set -- component state, then
 *                    controller state -- restores every value and display the
 *                    iPlug2 build reported, and the component state saved back
 *                    is the fixture's chunk byte for byte, which the iPlug2
 *                    build reads as its own
 *   the sound        the golden patch through the VST3's audio path: the Move
 *                    module's reference render, byte for byte
 *                    (tg_render_golden.h, as tg_render_ab holds the engine)
 *   the bypass       the host's Bypass passes the audio through bit for bit,
 *                    set through its parameter as a VST3 host sets it
 *   the window       opened while the transport runs, a set loaded on another
 *                    thread while it is open -- the window grows to the set's
 *                    32 steps -- closed and opened again at that size
 */
#include "tg_render_golden.h"
#include "trance_gate_core.h"

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

/* A transport playing from bar 1, its position accumulated block by block as
 * render_plugin.c accumulates it, so both renders see the same beats. */
struct Transport final : AudioPlayHead
{
    double rate = 48000.0, bpm = 120.0, beats = 0.0;
    int64 sample = 0;

    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true);
        p.setBpm (bpm);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setTimeInSamples (sample);
        p.setPpqPosition (beats);
        return p;
    }

    void advance (int frames)
    {
        sample += frames;
        beats += (frames / rate) * ((float) bpm / 60.0);
    }
};

std::unique_ptr<AudioPluginInstance> load (VST3PluginFormat& format, const PluginDescription& d, double rate, int block)
{
    String error;
    auto p = format.createInstanceFromDescription (d, rate, block, error);
    if (p != nullptr)
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

/* ---------------------------------------------------------------- checks -- */

void checkClass (const File& bundle, const File& fixtures)
{
    std::printf (" the class\n");
    const auto ids = JSON::parse (fixtures.getChildFile ("ids.json"));
    const auto info = JSON::parse (bundle.getChildFile ("Contents/Resources/moduleinfo.json"));
    const auto iplug2 = ids["NITranceGate"]["cid"].toString();
    String component;
    for (const auto& c : *info["Classes"].getArray())
        if (c["Category"].toString() == "Audio Module Class")
            component = c["CID"].toString();
    check (component == iplug2, "the component class is the iPlug2 build's", component + " / " + iplug2);
    const auto* compat = info["Compatibility"].getArray();
    check (compat == nullptr || compat->isEmpty(), "moduleinfo.json declares no compatibility");
}

void checkParameters (AudioPluginInstance& p, const File& dir)
{
    std::printf (" the parameters\n");
    const auto doc = JSON::parse (dir.getChildFile ("parameters.json"));
    const auto defaults = JSON::parse (dir.getChildFile ("default.json"));
    std::map<String, String> text;
    for (const auto& v : *defaults["values"].getArray())
        text[v["id"].toString()] = v["display"].toString();
    int count = 0;
    for (const auto& row : *doc["parameters"].getArray())
    {
        const auto id = row["id"].toString();
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
    const auto reopened = hostState (component, controller);
    p->setStateInformation (reopened.getData(), (int) reopened.getSize());

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

/*
 * THE GOLDEN PATCH, as a set would carry it: the parameters the patch sets,
 * and the engine's blob with its pattern, ties and step amounts -- made by
 * the engine itself from render_plugin.c's settings.
 */
MemoryBlock goldenState()
{
    tg_core_t* c = tg_core_create (TG_RENDER_SR);
    const char* settings[][2] { { "rate", "1/16" },       { "length", "15" },        { "pattern", "BEEF" },
                                { "ties", "0022" },       { "attack", "3.8267" },    { "decay", "43.7333" },
                                { "sustain", "0.6" },     { "release", "27.3333" },  { "hold", "0.75" },
                                { "amount", "0.9" } };
    for (const auto& kv : settings)
        tg_core_set_param (c, kv[0], kv[1]);
    for (int s = 0; s < 16; ++s)
    {
        tg_core_set_param (c, "cursor", String (s).toRawUTF8());
        tg_core_set_param (c, "step_amount", String (0.35 + 0.04 * s, 3).toRawUTF8());
    }
    tg_core_set_param (c, "cursor", "0");
    char blob[TG_STATE_MAX];
    const int n = tg_core_get_param (c, "state", blob, (int) sizeof blob);
    tg_core_destroy (c);

    /* The fifteen plain values: Slot 1, Length 16, 1/16, the defaults of
     * Join, Env Time and Curve, then the patch's, then Fade 100, Hard, In. */
    const double params[] { 1, 16, 7, 0, 0, 0, 90, 75, 3.8267, 43.7333, 60, 27.3333, 100, 0, 0 };
    MemoryBlock body;
    for (const double v : params)
        putF64 (body, v);
    putI32 (body, n > 0 ? n : 0);
    body.append (blob, (size_t) juce::jmax (0, n));
    MemoryBlock chunk;
    const uint8 magic[8] { 'N', 'I', 's', 't', 0x00, 0x00, 0xF8, 0x7F };
    chunk.append (magic, 8);
    putI32 (chunk, 1);
    putI32 (chunk, (int32) body.getSize());
    chunk.append (body.getData(), body.getSize());
    putI32 (chunk, 0);
    return hostState (chunk, {});
}

void checkSound (VST3PluginFormat& format, const PluginDescription& d)
{
    std::printf (" the sound\n");
    auto p = load (format, d, TG_RENDER_SR, TG_RENDER_BLOCK);
    if (p == nullptr)
        return check (false, "the plugin loads");
    const auto state = goldenState();
    p->setStateInformation (state.getData(), (int) state.getSize());
    Transport transport;
    transport.rate = TG_RENDER_SR;
    transport.bpm = TG_RENDER_BPM;
    p->setPlayHead (&transport);

    uint64 fnv = 0xcbf29ce484222325ULL;
    const int total = (int) (TG_RENDER_SECONDS * TG_RENDER_SR);
    const double w = 2.0 * MathConstants<double>::pi * 220.0 / TG_RENDER_SR;
    double phase = 0.0;
    AudioBuffer<float> buffer (2, TG_RENDER_BLOCK);
    MidiBuffer midi;
    for (int done = 0; done < total; done += TG_RENDER_BLOCK)
    {
        const int n = jmin (TG_RENDER_BLOCK, total - done);
        buffer.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            /* The input quantised to int16 first, as the Move's buffer is. */
            const auto q = (float) (short) std::lrint (22000.0 * std::sin (phase));
            phase += w;
            buffer.setSample (0, i, q);
            buffer.setSample (1, i, q);
        }
        p->processBlock (buffer, midi);
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < 2; ++ch)
            {
                const float r = std::round (buffer.getSample (ch, i));
                const auto s = (int16) (r > 32767.0f ? 32767.0f : (r < -32768.0f ? -32768.0f : r));
                const auto* b = reinterpret_cast<const unsigned char*> (&s);
                for (int k = 0; k < 2; ++k)
                {
                    fnv ^= b[k];
                    fnv *= 0x100000001b3ULL;
                }
            }
        transport.advance (n);
    }
    p->setPlayHead (nullptr);
    check (fnv == GOLDEN_FNV1A, "4 s through the VST3 match the Move module's reference render",
           String::toHexString ((int64) fnv) + " / " + String::toHexString ((int64) GOLDEN_FNV1A));
}

/* THE HOST'S BYPASS, as a VST3 host sets it: through the parameter, which
 * JUCE's wrapper hands to processBlock rather than bypassing the plugin
 * itself. The golden set gates a sine hard; bypassed, the sine comes out bit
 * for bit, and with the Bypass off again the gate is back. */
void checkBypass (VST3PluginFormat& format, const PluginDescription& d)
{
    std::printf (" the bypass\n");
    auto p = load (format, d, TG_RENDER_SR, TG_RENDER_BLOCK);
    if (p == nullptr)
        return check (false, "the plugin loads");
    const auto state = goldenState();
    p->setStateInformation (state.getData(), (int) state.getSize());
    Transport transport;
    transport.rate = TG_RENDER_SR;
    transport.bpm = TG_RENDER_BPM;
    p->setPlayHead (&transport);

    const double w = 2.0 * MathConstants<double>::pi * 220.0 / TG_RENDER_SR;
    double phase = 0.0;
    AudioBuffer<float> buffer (2, TG_RENDER_BLOCK), copy (2, TG_RENDER_BLOCK);
    MidiBuffer midi;
    /* Whether `blocks` blocks came out exactly as they went in. */
    const auto untouched = [&] (int blocks)
    {
        bool same = true;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < TG_RENDER_BLOCK; ++i, phase += w)
                for (int ch = 0; ch < 2; ++ch)
                    buffer.setSample (ch, i, (float) (0.5 * std::sin (phase)));
            copy.makeCopyOf (buffer);
            p->processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                same = same && std::memcmp (buffer.getReadPointer (ch), copy.getReadPointer (ch),
                                            sizeof (float) * (size_t) TG_RENDER_BLOCK) == 0;
            transport.advance (TG_RENDER_BLOCK);
        }
        return same;
    };
    const int twoSeconds = (int) (2.0 * TG_RENDER_SR) / TG_RENDER_BLOCK;

    check (! untouched (twoSeconds), "the golden set gates the sine");
    p->getBypassParameter()->setValueNotifyingHost (1.0f);
    check (untouched (twoSeconds), "with the host's Bypass on, two seconds pass through bit for bit");
    p->getBypassParameter()->setValueNotifyingHost (0.0f);
    check (! untouched (twoSeconds), "... and with it off again, the gate is back");
    p->setPlayHead (nullptr);
}

void checkWindow (VST3PluginFormat& format, const PluginDescription& d, const File& dir)
{
    std::printf (" the window\n");
    auto p = load (format, d, 48000.0, 512);
    if (p == nullptr)
        return check (false, "the plugin loads");
    Transport transport;
    p->setPlayHead (&transport);
    AudioBuffer<float> buffer (2, 512);
    MidiBuffer midi;
    const auto run = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            p->processBlock (buffer, midi);
            transport.advance (512);
            MessageManager::getInstance()->runDispatchLoopUntil (2);
        }
    };

    std::unique_ptr<AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    check (editor != nullptr, "the editor opens");
    if (editor == nullptr)
        return;
    run (50);
    const int sixteen = editor->getHeight();
    check (editor->getWidth() == 824 && sixteen == 784, "the window is the design's, 824 x 784 at 16 steps",
           String (editor->getWidth()) + " x " + String (sixteen));

    /* A set loaded on the host's own thread, the window open and the
     * transport running: its 32 steps grow the window by a row of pads. */
    const auto slots = hostState (fileBytes (dir.getChildFile ("slots.component.bin")),
                                  fileBytes (dir.getChildFile ("slots.controller.bin")));
    std::thread host ([&] { p->setStateInformation (slots.getData(), (int) slots.getSize()); });
    run (10);
    host.join();
    run (50);
    check (editor->getHeight() > sixteen, "a set of 32 steps loaded on another thread grows the open window",
           String (editor->getHeight()));
    check (std::abs (byId (*p, "7")->getValue() - 0.4736842f) < 1.0e-5f, "... and its Width reached the host");

    const int thirtyTwo = editor->getHeight();
    editor.reset();
    run (10);
    editor.reset (p->createEditorAndMakeActive());
    check (editor != nullptr, "the editor opens again");
    if (editor != nullptr)
    {
        run (20);
        check (editor->getHeight() == thirtyTwo, "... at the set's size", String (editor->getHeight()));
    }
    editor.reset();
    p->setPlayHead (nullptr);
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf (stderr, "usage: tg_host <NITranceGate.vst3> <fixtures-dir>\n");
        return 2;
    }
    const ScopedJuceInitialiser_GUI gui;
    const File bundle (String::fromUTF8 (argv[1]));
    const File fixtures (String::fromUTF8 (argv[2]));
    const auto dir = fixtures.getChildFile ("NITranceGate");
    std::printf ("tg_host %s\n", bundle.getFullPathName().toRawUTF8());

    VST3PluginFormat format;
    OwnedArray<PluginDescription> found;
    format.findAllTypesForFile (found, bundle.getFullPathName());
    check (found.size() == 1, "one class in the bundle", String (found.size()));
    if (found.isEmpty())
        return 1;
    const auto& d = *found[0];
    check (d.name == "NI Trance Gate" && d.manufacturerName == "Neon Ingvy", "NI Trance Gate, from Neon Ingvy",
           d.name + " / " + d.manufacturerName);

    checkClass (bundle, fixtures);
    if (auto p = load (format, d, 48000.0, 512))
        checkParameters (*p, dir);
    for (const char* scenario : { "default", "slots", "bypassed" })
        checkFixture (format, d, dir, scenario);
    checkSound (format, d);
    checkBypass (format, d);
    checkWindow (format, d, dir);

    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
