// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The built NISpectrogram.vst3, hosted the way a DAW hosts it: JUCE's own VST3
 * host, the bundle from build/out, nothing of the plugin's sources.
 *
 *   sg_host <NISpectrogram.vst3> <tests/fixtures/iplug2>
 *
 * WHAT ONLY THE BUNDLE CAN SHOW, after sg_processor has held the processor:
 *
 *   the class        the iPlug2 build's class ID (ids.json), as the factory and
 *                    moduleinfo.json report it, and no compatibility entry
 *   the parameter    through the VST3 controller: Bypass alone, the host's,
 *                    at ID 0, its name, steps, default and text the iPlug2
 *                    build's
 *   every fixture    loaded as Live reopens a set -- component state, then
 *                    controller state -- restores the bypass, and the
 *                    component state saved back is the fixture's chunk byte
 *                    for byte, which the iPlug2 build reads as its own; and a
 *                    set saved bypassed reopens bypassed
 *   the audio        through the VST3's audio path bit for bit, a running
 *                    transport and the host's bypass included
 *   the window       the design's size, opened while the transport runs, a
 *                    set loaded on another thread while it is open, closed
 *                    and opened again
 */
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
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

struct Transport final : AudioPlayHead
{
    double rate = 48000.0;
    int64 sample = 0;

    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true);
        p.setBpm (120.0);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setTimeInSamples (sample);
        p.setPpqPosition ((double) sample / rate * 2.0);
        return p;
    }
};

std::unique_ptr<AudioPluginInstance> load (VST3PluginFormat& format, const PluginDescription& d)
{
    String error;
    auto p = format.createInstanceFromDescription (d, 48000.0, 512, error);
    if (p != nullptr)
        p->prepareToPlay (48000.0, 512);
    return p;
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

/* ---------------------------------------------------------------- checks -- */

void checkClass (const File& bundle, const File& fixtures)
{
    std::printf (" the class\n");
    const auto ids = JSON::parse (fixtures.getChildFile ("ids.json"));
    const auto info = JSON::parse (bundle.getChildFile ("Contents/Resources/moduleinfo.json"));
    const auto iplug2 = ids["NISpectrogram"]["cid"].toString();
    String component;
    for (const auto& c : *info["Classes"].getArray())
        if (c["Category"].toString() == "Audio Module Class")
        {
            component = c["CID"].toString();
            StringArray sub;
            if (const auto* list = c["Sub Categories"].getArray())
                for (const auto& v : *list)
                    sub.add (v.toString());
            check (sub.joinIntoString ("|") == "Fx|Analyzer", "Fx|Analyzer, as the iPlug2 build was",
                   sub.joinIntoString ("|"));
        }
    check (component == iplug2, "the component class is the iPlug2 build's", component + " / " + iplug2);
    const auto* compat = info["Compatibility"].getArray();
    check (compat == nullptr || compat->isEmpty(), "moduleinfo.json declares no compatibility");
}

void checkParameter (AudioPluginInstance& p, const File& dir)
{
    std::printf (" the parameter\n");
    const auto doc = JSON::parse (dir.getChildFile ("parameters.json"));
    const auto defaults = JSON::parse (dir.getChildFile ("default.json"));
    const auto& row = doc["parameters"][0];
    check (p.getParameters().size() == 1, "one parameter", String (p.getParameters().size()));
    auto* bypass = dynamic_cast<HostedAudioProcessorParameter*> (p.getBypassParameter());
    check (bypass != nullptr && bypass->getParameterID() == "0", "Bypass is the host's, at ID 0",
           bypass != nullptr ? bypass->getParameterID() : String ("none"));
    if (bypass == nullptr)
        return;
    const auto want = row["title"].toString() + " [" + row["units"].toString() + "] " + row["stepCount"].toString()
                    + " steps, default " + String ((double) row["defaultNormalizedValue"], 6) + " \""
                    + defaults["values"][0]["display"].toString() + "\"";
    const auto got = bypass->getName (128) + " [" + bypass->getLabel() + "] "
                   + String (bypass->isDiscrete() ? bypass->getNumSteps() - 1 : 0) + " steps, default "
                   + String (bypass->getDefaultValue(), 6) + " \""
                   + bypass->getText (bypass->getDefaultValue(), 128) + "\"";
    check (want == got, "Bypass as the iPlug2 build reported it", want == got ? String() : got + " != " + want);
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
    auto* bypass = p->getBypassParameter();
    const auto& v = doc["values"][0];
    check (bypass != nullptr && std::abs (bypass->getValue() - (double) v["normalized"]) < 1.0e-6
               && bypass->getText (bypass->getValue(), 128) == v["display"].toString(),
           "the bypass the iPlug2 build reported");
    check (componentChunk (*p) == component, "the state saved back is the iPlug2 chunk, byte for byte");
}

/* The session fixture with its bypass flag on: the int32 after the body. */
void checkBypassed (VST3PluginFormat& format, const PluginDescription& d, const File& dir)
{
    std::printf (" bypassed\n");
    auto component = fileBytes (dir.getChildFile ("session.component.bin"));
    static_cast<char*> (component.getData())[component.getSize() - 4] = 1;
    auto p = load (format, d);
    if (p == nullptr)
        return check (false, "the plugin loads");
    const auto reopened = hostState (component, {});
    p->setStateInformation (reopened.getData(), (int) reopened.getSize());
    check (p->getBypassParameter() != nullptr && p->getBypassParameter()->getValue() >= 0.5f,
           "a set saved bypassed reopens bypassed");
    check (componentChunk (*p) == component, "... and saves so again, byte for byte");
}

void checkAudio (VST3PluginFormat& format, const PluginDescription& d)
{
    std::printf (" the audio\n");
    auto p = load (format, d);
    if (p == nullptr)
        return check (false, "the plugin loads");
    Transport transport;
    p->setPlayHead (&transport);
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> any (-1.0f, 1.0f);
    AudioBuffer<float> buffer (2, 512), copy (2, 512);
    MidiBuffer midi;
    bool same = true;
    for (int b = 0; b < 400; ++b)
    {
        if (b == 200)
            p->getBypassParameter()->setValueNotifyingHost (1.0f);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (ch, i, any (rng));
        copy.makeCopyOf (buffer);
        p->processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            same = same && std::memcmp (buffer.getReadPointer (ch), copy.getReadPointer (ch), 512 * sizeof (float)) == 0;
        transport.sample += 512;
        if (b % 8 == 0)
            MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
    check (same, "four seconds of noise through the VST3, bit for bit, two of them bypassed");
    p->setPlayHead (nullptr);
}

void checkWindow (VST3PluginFormat& format, const PluginDescription& d, const File& dir)
{
    std::printf (" the window\n");
    auto p = load (format, d);
    if (p == nullptr)
        return check (false, "the plugin loads");
    Transport transport;
    p->setPlayHead (&transport);
    AudioBuffer<float> buffer (2, 512);
    MidiBuffer midi;
    double phase = 0.0;
    const auto run = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const auto v = (float) (0.5 * std::sin (phase));
                phase += 2.0 * MathConstants<double>::pi * 440.0 / 48000.0;
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            p->processBlock (buffer, midi);
            transport.sample += 512;
            MessageManager::getInstance()->runDispatchLoopUntil (2);
        }
    };

    std::unique_ptr<AudioProcessorEditor> editor (p->createEditorAndMakeActive());
    check (editor != nullptr, "the editor opens");
    if (editor == nullptr)
        return;
    run (50);
    check (editor->getWidth() == 720 && editor->getHeight() == 502, "the window is the design's, 720 x 502",
           String (editor->getWidth()) + " x " + String (editor->getHeight()));

    /* A set loaded on the host's own thread, the window open and the
     * transport running. */
    const auto session = hostState (fileBytes (dir.getChildFile ("session.component.bin")),
                                    fileBytes (dir.getChildFile ("session.controller.bin")));
    std::thread host ([&] { p->setStateInformation (session.getData(), (int) session.getSize()); });
    run (10);
    host.join();
    run (50);
    check (componentChunk (*p) == fileBytes (dir.getChildFile ("session.component.bin")),
           "a set loaded on another thread while it is open is the set it saves");

    editor.reset();
    run (10);
    editor.reset (p->createEditorAndMakeActive());
    check (editor != nullptr, "the editor opens again");
    if (editor != nullptr)
    {
        run (20);
        check (editor->getWidth() == 720 && editor->getHeight() == 502, "... at the design's size");
    }
    editor.reset();
    p->setPlayHead (nullptr);
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf (stderr, "usage: sg_host <NISpectrogram.vst3> <fixtures-dir>\n");
        return 2;
    }
    const ScopedJuceInitialiser_GUI gui;
    const File bundle (String::fromUTF8 (argv[1]));
    const File fixtures (String::fromUTF8 (argv[2]));
    const auto dir = fixtures.getChildFile ("NISpectrogram");
    std::printf ("sg_host %s\n", bundle.getFullPathName().toRawUTF8());

    VST3PluginFormat format;
    OwnedArray<PluginDescription> found;
    format.findAllTypesForFile (found, bundle.getFullPathName());
    check (found.size() == 1, "one class in the bundle", String (found.size()));
    if (found.isEmpty())
        return 1;
    const auto& d = *found[0];
    check (d.name == "NI Spectrogram" && d.manufacturerName == "Neon Ingvy", "NI Spectrogram, from Neon Ingvy",
           d.name + " / " + d.manufacturerName);

    checkClass (bundle, fixtures);
    if (auto p = load (format, d))
        checkParameter (*p, dir);
    for (const char* scenario : { "default", "session" })
        checkFixture (format, d, dir, scenario);
    checkBypassed (format, d, dir);
    checkAudio (format, d);
    checkWindow (format, d, dir);

    std::printf ("%s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
