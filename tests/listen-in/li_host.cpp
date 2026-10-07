// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The built NIListenIn.vst3, hosted the way a DAW hosts it: JUCE's own VST3
 * host, the bundle from build/out, nothing of the plugin's sources -- and the
 * bus read back through its own C ABI, as a Spectrogram reads it.
 *
 *   li_host <NIListenIn.vst3> <tests/fixtures/iplug2>
 *
 * WHAT ONLY THE BUNDLE CAN SHOW, after li_processor has held the processor:
 *
 *   the class        the iPlug2 build's class ID (ids.json), as moduleinfo.json
 *                    reports it, and no compatibility entry
 *   the parameters   through the VST3 controller: Bus at the iPlug2 build's
 *                    ID 0 with its name, steps, default and text; Bypass the
 *                    host's, at ID 1
 *   every fixture    loaded as Live reopens a set restores the bus, its text
 *                    and the bypass the iPlug2 build reported, and the
 *                    component state saved back is the fixture's chunk byte
 *                    for byte
 *   the bus          a reopened set claims its bus under its name once the
 *                    message loop runs; the audio passes through bit for bit
 *                    and arrives on the bus as it came; with the host's Bypass
 *                    on, the audio passes and nothing is published
 *   the window       360 x 172, open while a set loads on another thread,
 *                    closed and opened again
 */
#include "audio_bus.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
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

std::unique_ptr<AudioPluginInstance> load (VST3PluginFormat& format, const PluginDescription& d)
{
    String error;
    auto p = format.createInstanceFromDescription (d, 48000.0, 512, error);
    if (p != nullptr)
        p->prepareToPlay (48000.0, 512);
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

/* IComponent::getState's chunk, with the JUCE wrapper's private trailer --
 * 16 bytes and "JUCEPrivateData" after it -- taken off. */
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

void idle (int ms)
{
    MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

/* What a receiver sees of a bus without opening it. */
struct Seen
{
    bool live = false;
    std::string label;
};

Seen probe (int bus)
{
    Seen s;
    int32_t live = 0;
    uint32_t rate = 0;
    char label[ABUS_LABEL_CAP] {};
    abus_probe ((uint32_t) bus, &live, &rate, label, ABUS_LABEL_CAP);
    s.live = live != 0;
    s.label = label;
    return s;
}

/* Everything waiting on a reader, interleaved. */
std::vector<float> drain (abus_reader_t* r)
{
    std::vector<float> out, chunk (4096 * 2);
    while (const auto n = abus_reader_read (r, chunk.data(), 4096, nullptr, nullptr))
        out.insert (out.end(), chunk.begin(), chunk.begin() + (std::ptrdiff_t) n * 2);
    return out;
}

/* ---------------------------------------------------------------- checks -- */

void checkClass (const File& bundle, const File& fixtures)
{
    std::printf (" the class\n");
    const auto ids = JSON::parse (fixtures.getChildFile ("ids.json"));
    const auto info = JSON::parse (bundle.getChildFile ("Contents/Resources/moduleinfo.json"));
    const auto iplug2 = ids["NIListenIn"]["cid"].toString();
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
    check (count == 2 && p.getParameters().size() == 2, "Bus and Bypass, and nothing else", String (p.getParameters().size()));
    auto* bypass = dynamic_cast<HostedAudioProcessorParameter*> (p.getBypassParameter());
    check (bypass != nullptr && bypass->getParameterID() == "1", "Bypass is the host's, at ID 1");
}

void checkFixture (VST3PluginFormat& format, const PluginDescription& d, const File& dir, const String& scenario)
{
    std::printf (" %s\n", scenario.toRawUTF8());
    const auto doc = JSON::parse (dir.getChildFile (scenario + ".json"));
    const auto component = fileBytes (dir.getChildFile (scenario + ".component.bin"));
    const auto controller = fileBytes (dir.getChildFile (scenario + ".controller.bin"));
    auto p = load (format, d);
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

    /* The set's bus, claimed under the set's name by the plugin's own timer. */
    const int bus = (int) (*doc["decoded"]["params"].getArray())[0];
    const auto name = (*doc["decoded"]["strings"].getArray())[0].toString();
    idle (300);
    const auto seen = probe (bus);
    check (seen.live && String::fromUTF8 (seen.label.c_str()) == name, "bus " + String (bus) + " is live under the set's name",
           String::fromUTF8 (seen.label.c_str()));
}

void checkBus (VST3PluginFormat& format, const PluginDescription& d)
{
    std::printf (" the bus\n");
    auto p = load (format, d);
    if (p == nullptr)
        return check (false, "the plugin loads");
    auto* bus = byId (*p, "0");
    bus->setValueNotifyingHost (bus->getValueForText ("6"));
    AudioBuffer<float> buffer (2, 512);
    MidiBuffer midi;
    buffer.clear();
    p->processBlock (buffer, midi);
    idle (300);
    check (probe (6).live, "moving Bus to 6 claims bus 6");

    abus_reader_t* reader = nullptr;
    if (abus_reader_open (6, &reader) != ABUS_OK)
        return check (false, "a reader opens bus 6");

    std::mt19937 rng (3);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
    std::vector<float> sent;
    bool through = true;
    for (int b = 0; b < 8; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (ch, i, noise (rng));
        const AudioBuffer<float> in (buffer);
        p->processBlock (buffer, midi);
        for (int i = 0; i < 512; ++i)
        {
            sent.push_back (in.getSample (0, i));
            sent.push_back (in.getSample (1, i));
            through = through && exactlyEqual (buffer.getSample (0, i), in.getSample (0, i))
                   && exactlyEqual (buffer.getSample (1, i), in.getSample (1, i));
        }
    }
    check (through, "the audio passes through bit for bit");
    const auto got = drain (reader);
    check (got == sent, "and arrives on the bus as it came", String ((int) got.size() / 2) + " frames");

    /* The host's Bypass, as a VST3 host sets it: JUCE's wrapper still calls
     * processBlock, and the plugin must bypass itself. */
    p->getBypassParameter()->setValueNotifyingHost (1.0f);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample (ch, i, noise (rng));
    const AudioBuffer<float> in (buffer);
    p->processBlock (buffer, midi);
    bool same = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            same = same && exactlyEqual (buffer.getSample (ch, i), in.getSample (ch, i));
    check (same, "bypassed, the audio passes through");
    check (drain (reader).empty(), "... and nothing is published");
    abus_reader_close (reader);
}

void checkWindow (VST3PluginFormat& format, const PluginDescription& d, const File& dir)
{
    std::printf (" the window\n");
    auto p = load (format, d);
    if (p == nullptr)
        return check (false, "the plugin loads");
    std::unique_ptr<AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    check (editor != nullptr, "the editor opens");
    if (editor == nullptr)
        return;
    idle (50);
    check (editor->getWidth() == 360 && editor->getHeight() == 172, "the window is the design's, 360 x 172",
           String (editor->getWidth()) + " x " + String (editor->getHeight()));

    const auto labelled = hostState (fileBytes (dir.getChildFile ("labelled.component.bin")),
                                     fileBytes (dir.getChildFile ("labelled.controller.bin")));
    std::thread host ([&] { p->setStateInformation (labelled.getData(), (int) labelled.getSize()); });
    idle (20);
    host.join();
    idle (300);
    check (byId (*p, "0")->getText (byId (*p, "0")->getValue(), 128) == "3", "a set loaded on another thread reaches the host");
    check (probe (3).live && probe (3).label == "Bässe & Kick", "... and claims its bus while the window is open");

    editor.reset();
    idle (20);
    editor.reset (p->createEditorAndMakeActive());
    check (editor != nullptr, "the editor opens again");
    idle (50);
    editor.reset();
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf (stderr, "usage: li_host <NIListenIn.vst3> <fixtures-dir>\n");
        return 2;
    }
    /* A bus namespace of this process's own, which the plugin loaded into it
     * shares: set before the first bus call on either side. */
    const auto ns = "li_host." + std::to_string ((long) getpid());
    setenv ("NIA_BUS_NS", ns.c_str(), 1);

    const ScopedJuceInitialiser_GUI gui;
    const File bundle (String::fromUTF8 (argv[1]));
    const File fixtures (String::fromUTF8 (argv[2]));
    const auto dir = fixtures.getChildFile ("NIListenIn");
    std::printf ("li_host %s\n", bundle.getFullPathName().toRawUTF8());

    VST3PluginFormat format;
    OwnedArray<PluginDescription> found;
    format.findAllTypesForFile (found, bundle.getFullPathName());
    check (found.size() == 1, "one class in the bundle", String (found.size()));
    if (found.isEmpty())
        return 1;
    const auto& d = *found[0];
    check (d.name == "NI Listen-In" && d.manufacturerName == "Neon Ingvy", "NI Listen-In, from Neon Ingvy",
           d.name + " / " + d.manufacturerName);
    check (d.category == "Fx|Tools", "filed as Fx|Tools, as the iPlug2 build was", d.category);

    checkClass (bundle, fixtures);
    if (auto p = load (format, d))
        checkParameters (*p, dir);
    for (const char* scenario : { "default", "labelled" })
        checkFixture (format, d, dir, scenario);
    checkBus (format, d);
    checkWindow (format, d, dir);

    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
