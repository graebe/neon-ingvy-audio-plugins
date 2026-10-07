// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A JUCE plugin's bundle, loaded the way a DAW loads it: JUCE's own VST3
 * host, the built .vst3 from build/out, nothing of the plugin's sources.
 *
 *   juce_host <bundle.vst3> "<plugin name>"
 *
 * WHAT A HOST NEEDS, AND NOTHING A PRODUCT'S OWN TESTS ALREADY HOLD:
 *
 *   it is there      one class, by the name the build gave it, from Neon Ingvy
 *   it runs          prepared, a second of blocks with MIDI in every one (an
 *                    instrument's notes, a pedal, a panic) at two block sizes,
 *                    released and prepared again at another rate
 *   it saves         every parameter set to a value of its own, the state
 *                    saved, a second instance loaded from it: the same values,
 *                    and the same state saved back
 *   it opens         its editor, at a size, created and destroyed twice
 *
 * Every product on the JUCE shell runs this (tests/CMakeLists.txt), so a
 * product moving onto the shell gets it by being built.
 */
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <memory>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    if (! ok)
    {
        std::fprintf (stderr, "FAILED: %s\n", what.toRawUTF8());
        ++failures;
    }
}

std::unique_ptr<juce::AudioPluginInstance> load (juce::VST3PluginFormat& format, const juce::PluginDescription& d,
                                                 double rate, int block)
{
    juce::String error;
    auto instance = format.createInstanceFromDescription (d, rate, block, error);
    check (instance != nullptr, "the class instantiates: " + error);
    return instance;
}

void run (juce::AudioPluginInstance& p, double rate, int block, int seconds)
{
    p.prepareToPlay (rate, block);
    juce::AudioBuffer<float> buffer (juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), block);
    juce::MidiBuffer midi;
    const int blocks = (int) (seconds * rate / block);
    for (int i = 0; i < blocks; ++i)
    {
        buffer.clear();
        midi.clear();
        const int note = 48 + (i % 24);
        midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOn (2, note + 4, (juce::uint8) 90), block / 3);
        midi.addEvent (juce::MidiMessage::noteOff (1, note), block - 1);
        if (i % 16 == 5)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), block / 2);
        if (i % 16 == 9)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), block / 2);
        if (i % 64 == 63)
            midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        p.processBlock (buffer, midi);
    }
    p.releaseResources();
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf (stderr, "usage: juce_host <bundle.vst3> \"<plugin name>\"\n");
        return 2;
    }
    const juce::ScopedJuceInitialiser_GUI gui;
    const juce::String bundle = juce::String::fromUTF8 (argv[1]);
    const juce::String name = juce::String::fromUTF8 (argv[2]);

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, bundle);
    check (found.size() == 1, "one class in " + bundle + ", found " + juce::String (found.size()));
    if (found.isEmpty())
        return 1;
    const auto& d = *found[0];
    check (d.name == name, "named '" + name + "', not '" + d.name + "'");
    check (d.manufacturerName == "Neon Ingvy", "made by Neon Ingvy, not '" + d.manufacturerName + "'");

    /* It runs. */
    auto first = load (format, d, 48000.0, 512);
    if (first == nullptr)
        return 1;
    run (*first, 48000.0, 512, 1);
    run (*first, 44100.0, 64, 1);

    /* It saves: every parameter to a value of its own. */
    auto& params = first->getParameters();
    check (! params.isEmpty(), "it has parameters");
    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = params[i];
        const int steps = juce::jmax (2, p->getNumSteps());
        const float v = (float) ((i % (steps - 1)) + 1) / (float) (steps - 1);
        p->setValueNotifyingHost (v);
    }
    juce::MemoryBlock saved;
    first->getStateInformation (saved);
    check (saved.getSize() > 0, "the state is not empty");

    auto second = load (format, d, 48000.0, 512);
    if (second == nullptr)
        return 1;
    second->setStateInformation (saved.getData(), (int) saved.getSize());
    for (int i = 0; i < params.size() && i < second->getParameters().size(); ++i)
        check (std::abs (second->getParameters()[i]->getValue() - params[i]->getValue()) < 1.0e-4f,
               "parameter " + juce::String (i) + " (" + params[i]->getName (32) + ") survives a round trip");
    juce::MemoryBlock again;
    second->getStateInformation (again);
    check (again == saved, "the state saves back as it was loaded");

    /* It opens. */
    for (int round = 0; round < 2; ++round)
    {
        check (first->hasEditor(), "it has an editor");
        std::unique_ptr<juce::AudioProcessorEditor> editor (first->createEditorAndMakeActive());
        check (editor != nullptr, "the editor opens");
        if (editor != nullptr)
            check (editor->getWidth() > 0 && editor->getHeight() > 0, "the editor has a size");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    if (failures == 0)
        std::printf ("juce_host: %s passes\n", name.toRawUTF8());
    return failures == 0 ? 0 : 1;
}
