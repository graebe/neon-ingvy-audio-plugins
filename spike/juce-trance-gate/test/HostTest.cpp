// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The built spike, hosted the way a DAW hosts it, against the iPlug2 build it
 * replaces.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Torben Gräber
 *
 *   spike_host <spike.vst3> <tests/fixtures/iplug2> <iPlug2 NITranceGate.vst3>
 *
 * JUCE's own VST3 host loads both bundles -- the class, the controller, the
 * connection between them and the state calls all go through the VST3
 * interfaces, as in Live -- and checks, in order:
 *
 *   the class        moduleinfo.json declares the iPlug2 class compatible
 *                    (Old) with this one (New), and Old is the class ID the
 *                    iPlug2 factory reports (ids.json) -- or, built with
 *                    NI_SPIKE_SAME_CLASS, this build's class IS that ID;
 *   the parameters   the same IDs, names, units, steps, defaults and default
 *                    texts as the iPlug2 build's (parameters.json), Bypass
 *                    apart: its ID is 15 here, 65536 there;
 *   every fixture    loaded as Live reopens a set -- component state, then
 *                    controller state -- restores every parameter the iPlug2
 *                    build reported, Bypass included, and the engine's blob;
 *   the way back     what this build then saves loads into the iPlug2 build
 *                    with the same parameters and blob;
 *   the sound        both builds, the slots fixture loaded and the same
 *                    transport running, turn the same input into the same
 *                    output, and that output is gated.
 */
#include "Chunk.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>

using namespace juce;

namespace {

int gFails = 0;

void Check(bool ok, const String& what, const String& detail = {})
{
  std::printf("  %-66s %s%s\n", what.toRawUTF8(), ok ? "ok" : "FAIL",
              detail.isEmpty() ? "" : (" (" + detail + ")").toRawUTF8());
  if (!ok)
    gFails++;
}

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;
/* iPlug2's VST3 ID for its bypass parameter. */
const String kIPlug2Bypass = "65536";

/* A transport playing from bar 1 at 120 BPM in 4/4, advanced by the caller. */
struct Transport final : AudioPlayHead
{
  int64 sample = 0;

  Optional<PositionInfo> getPosition() const override
  {
    PositionInfo p;
    p.setIsPlaying(true);
    p.setBpm(120.0);
    p.setTimeSignature(TimeSignature{4, 4});
    p.setTimeInSamples(sample);
    p.setTimeInSeconds(double(sample) / kSampleRate);
    p.setPpqPosition(double(sample) / kSampleRate * 2.0);
    return p;
  }
};

std::unique_ptr<AudioPluginInstance> Load(AudioPluginFormatManager& formats, const String& bundle)
{
  OwnedArray<PluginDescription> types;
  for (auto* format : formats.getFormats())
    format->findAllTypesForFile(types, bundle);
  if (types.isEmpty())
    return {};
  String error;
  auto instance = formats.createPluginInstance(*types[0], kSampleRate, kBlock, error);
  if (instance)
    instance->prepareToPlay(kSampleRate, kBlock);
  return instance;
}

/* A hosted parameter by its VST3 ID. */
AudioProcessorParameter* ById(AudioPluginInstance& p, const String& id)
{
  for (auto* param : p.getParameters())
    if (auto* hosted = dynamic_cast<HostedAudioProcessorParameter*>(param))
      if (hosted->getParameterID() == id)
        return param;
  return nullptr;
}

/* The spike's counterpart of an iPlug2 parameter ID: the same, but Bypass. */
AudioProcessorParameter* Counterpart(AudioPluginInstance& spike, const String& iplug2Id)
{
  return iplug2Id == kIPlug2Bypass ? spike.getBypassParameter() : ById(spike, iplug2Id);
}

/*
 * WHAT A VST3 HOST KEEPS OF AN INSTANCE, in the form JUCE's VST3 host reads
 * back: the component's state and the controller's, as Live holds them.
 */
MemoryBlock HostState(const MemoryBlock& component, const MemoryBlock& controller)
{
  XmlElement state("VST3PluginState");
  state.createNewChildElement("IComponent")->addTextElement(component.toBase64Encoding());
  state.createNewChildElement("IEditController")->addTextElement(controller.toBase64Encoding());
  MemoryBlock out;
  AudioProcessor::copyXmlToBinary(state, out);
  return out;
}

/* IComponent::getState's bytes, from what JUCE's host saved. */
MemoryBlock ComponentState(AudioPluginInstance& p)
{
  MemoryBlock saved, component;
  p.getStateInformation(saved);
  if (auto xml = AudioProcessor::getXmlFromBinary(saved.getData(), int(saved.getSize())))
    if (auto* c = xml->getChildByName("IComponent"))
      component.fromBase64Encoding(c->getAllSubText());
  return component;
}

/*
 * THE CHUNK INSIDE A COMPONENT STATE. A JUCE build appends JUCE's private
 * trailer after it -- 16 bytes and "JUCEPrivateData" -- which its wrapper
 * strips before the plugin reads; iPlug2 stops at the bypass and never sees
 * it. Taken off here the way the wrapper does, so the chunk can be decoded.
 */
std::optional<ni::tg::chunk::State> Chunk(const MemoryBlock& component)
{
  const char* kMagic = "JUCEPrivateData";
  const size_t magic = std::strlen(kMagic);
  size_t size = component.getSize();
  const auto* bytes = static_cast<const char*>(component.getData());
  if (size >= magic + 16 && std::memcmp(bytes + size - magic, kMagic, magic) == 0)
  {
    uint64 privateBytes;
    std::memcpy(&privateBytes, bytes + size - magic - 8, 8);
    size -= magic + 16 + size_t(privateBytes);
  }
  return ni::tg::chunk::Read(bytes, size);
}

MemoryBlock ReadFile(const File& f)
{
  MemoryBlock b;
  f.loadFileAsData(b);
  return b;
}

/* ------------------------------------------------------------- checks */

void CheckClass(const File& bundle, const File& fixtures)
{
  std::printf(" the class\n");
  const var info = JSON::parse(bundle.getChildFile("Contents/Resources/moduleinfo.json"));
  const var ids = JSON::parse(fixtures.getChildFile("ids.json"));
  const String iplug2 = ids["NITranceGate"]["cid"].toString();
  Check(iplug2 == NI_IPLUG2_TG_CLASS, "the build names the class the iPlug2 factory reports",
        iplug2 + " / " + NI_IPLUG2_TG_CLASS);

  String component;
  if (auto* classes = info["Classes"].getArray())
    for (const auto& c : *classes)
      if (c["Category"].toString() == "Audio Module Class")
        component = c["CID"].toString();
  Check(component.length() == 32, "moduleinfo.json lists this build's component class", component);

#if NI_SPIKE_SAME_CLASS
  /* The variant that takes the iPlug2 class over outright: nothing to map. */
  Check(component == iplug2, "this build's component class IS the iPlug2 class", component);
  Check(info["Compatibility"].getArray() == nullptr || info["Compatibility"].getArray()->isEmpty(),
        "moduleinfo.json declares no compatibility", JSON::toString(info["Compatibility"], true));
#else
  bool listed = false;
  if (auto* entries = info["Compatibility"].getArray())
    for (const auto& entry : *entries)
      if (entry["New"].toString() == component)
        if (auto* old = entry["Old"].getArray())
          listed = old->contains(var(iplug2));
  Check(listed, "moduleinfo.json: New is this build, Old the iPlug2 class",
        JSON::toString(info["Compatibility"], true));
#endif
}

void CheckParameters(AudioPluginInstance& spike, const File& fixtures)
{
  std::printf(" the parameters\n");
  const var doc = JSON::parse(fixtures.getChildFile("NITranceGate/parameters.json"));
  const var defaults = JSON::parse(fixtures.getChildFile("NITranceGate/default.json"));
  std::map<String, String> text;
  if (auto* values = defaults["values"].getArray())
    for (const auto& v : *values)
      text[v["id"].toString()] = v["display"].toString();

  int count = 0;
  if (auto* params = doc["parameters"].getArray())
    for (const auto& p : *params)
    {
      const String id = p["id"].toString();
      auto* param = Counterpart(spike, id);
      if (!param)
      {
        Check(false, "parameter " + id + " exists");
        continue;
      }
      count++;
      const int steps = param->isDiscrete() ? param->getNumSteps() - 1 : 0;
      const String want = p["title"].toString() + " [" + p["units"].toString() + "] " +
                          p["stepCount"].toString() + " steps, default " +
                          String(double(p["defaultNormalizedValue"]), 6) + " \"" + text[id] + "\"";
      const String got = param->getName(128) + " [" + param->getLabel() + "] " + String(steps) +
                         " steps, default " + String(param->getDefaultValue(), 6) + " \"" +
                         param->getText(param->getDefaultValue(), 128) + "\"";
      Check(want == got, "parameter " + id + ": " + p["title"].toString(), want == got ? "" : got + " != " + want);
    }
  Check(count == 16, "all fifteen and Bypass", String(count));
}

/*
 * ONE FIXTURE INTO A FRESH SPIKE, AS LIVE REOPENS A SET -- and then what the
 * spike saves, into a fresh iPlug2 instance.
 */
void CheckFixture(AudioPluginFormatManager& formats, const String& spikeBundle, const String& iplug2Bundle,
                  const File& fixtures, const String& scenario)
{
  std::printf(" %s\n", scenario.toRawUTF8());
  const File dir = fixtures.getChildFile("NITranceGate");
  const var doc = JSON::parse(dir.getChildFile(scenario + ".json"));
  const MemoryBlock component = ReadFile(dir.getChildFile(scenario + ".component.bin"));
  const MemoryBlock controller = ReadFile(dir.getChildFile(scenario + ".controller.bin"));
  const auto fixture = ni::tg::chunk::Read(component.getData(), component.getSize());

  auto spike = Load(formats, spikeBundle);
  Check(spike != nullptr, "the spike loads");
  if (!spike)
    return;
  const MemoryBlock reopened = HostState(component, controller);
  spike->setStateInformation(reopened.getData(), int(reopened.getSize()));

  /* Every parameter a host reads back: what the iPlug2 build reported, to a
   * float's precision, which is what a JUCE parameter holds. */
  String wrong;
  if (auto* values = doc["values"].getArray())
    for (const auto& v : *values)
    {
      const String id = v["id"].toString();
      auto* param = Counterpart(*spike, id);
      const double want = v["normalized"];
      if (!param || std::abs(param->getValue() - want) > 1e-6 ||
          param->getText(param->getValue(), 128) != v["display"].toString())
        wrong << " " << id << "=" << (param ? param->getText(param->getValue(), 128) : "?") << "!="
              << v["display"].toString();
    }
  Check(wrong.isEmpty(), "every parameter reads what the iPlug2 build saved", wrong.trim());

  const auto saved = Chunk(ComponentState(*spike));
  Check(saved.has_value(), "the spike saves an iPlug2 chunk");
  if (!saved || !fixture)
    return;
  Check(saved->blob == fixture->blob, "... with the same engine blob, every slot's pattern and sound");
  Check(saved->bypass == fixture->bypass, "... and the same bypass");
  double worst = 0.0;
  for (int i = 0; i < ni::tg::kNumParams; i++)
    worst = std::max(worst, std::abs(saved->params[i] - fixture->params[i]));
  Check(worst < 1e-5, "... and the same parameters, to a float's precision", String(worst));

  /* THE WAY BACK: the spike's state, as Live would hold it, into iPlug2. */
  auto iplug2 = Load(formats, iplug2Bundle);
  Check(iplug2 != nullptr, "the iPlug2 build loads", iplug2Bundle);
  if (!iplug2)
    return;
  const MemoryBlock spikeComponent = ComponentState(*spike);
  const MemoryBlock back = HostState(spikeComponent, {});
  iplug2->setStateInformation(back.getData(), int(back.getSize()));
  wrong.clear();
  if (auto* values = doc["values"].getArray())
    for (const auto& v : *values)
    {
      auto* param = ById(*iplug2, v["id"].toString());
      if (!param || std::abs(param->getValue() - double(v["normalized"])) > 1e-6)
        wrong << " " << v["id"].toString();
    }
  Check(wrong.isEmpty(), "the iPlug2 build opens what the spike saved, same parameters", wrong.trim());
  const auto again = Chunk(ComponentState(*iplug2));
  Check(again && again->blob == fixture->blob, "... and saves the same engine blob again");
}

/* Both builds, the same set, the same transport and input: the same output. */
void CheckSound(AudioPluginFormatManager& formats, const String& spikeBundle, const String& iplug2Bundle,
                const File& fixtures)
{
  std::printf(" the sound\n");
  const File dir = fixtures.getChildFile("NITranceGate");
  const MemoryBlock state = HostState(ReadFile(dir.getChildFile("slots.component.bin")),
                                      ReadFile(dir.getChildFile("slots.controller.bin")));
  auto spike = Load(formats, spikeBundle);
  auto iplug2 = Load(formats, iplug2Bundle);
  if (!spike || !iplug2)
  {
    Check(false, "both builds load");
    return;
  }
  Transport ta, tb;
  spike->setPlayHead(&ta);
  iplug2->setPlayHead(&tb);
  spike->setStateInformation(state.getData(), int(state.getSize()));
  iplug2->setStateInformation(state.getData(), int(state.getSize()));

  /* Two seconds: four bars of slot 2's 32 steps at 1/16 and 120 BPM is the
   * whole pattern twice. A steady input, so the output IS the gate. */
  AudioBuffer<float> a(2, kBlock), b(2, kBlock);
  MidiBuffer midi;
  double worst = 0.0;
  float lo = 1.0f, hi = 0.0f;
  for (int block = 0; block < int(2.0 * kSampleRate) / kBlock; block++)
  {
    for (auto* buffer : {&a, &b})
      for (int ch = 0; ch < 2; ch++)
        for (int i = 0; i < kBlock; i++)
          buffer->setSample(ch, i, 0.5f);
    spike->processBlock(a, midi);
    iplug2->processBlock(b, midi);
    for (int ch = 0; ch < 2; ch++)
      for (int i = 0; i < kBlock; i++)
      {
        worst = std::max(worst, double(std::abs(a.getSample(ch, i) - b.getSample(ch, i))));
        lo = std::min(lo, std::abs(a.getSample(ch, i)));
        hi = std::max(hi, std::abs(a.getSample(ch, i)));
      }
    ta.sample += kBlock;
    tb.sample += kBlock;
  }
  Check(worst == 0.0, "the spike's output is the iPlug2 build's, sample for sample", String(worst));
  Check(lo < 0.01f && hi > 0.45f, "... and it is gated: open to the input, closed to silence",
        "min " + String(lo, 4) + ", max " + String(hi, 4));
  spike->setPlayHead(nullptr);
  iplug2->setPlayHead(nullptr);
}

} // namespace

int main(int argc, char* argv[])
{
  if (argc != 4)
  {
    std::fprintf(stderr, "usage: spike_host <spike.vst3> <fixtures-dir> <iPlug2 NITranceGate.vst3>\n");
    return 2;
  }
  const ScopedJuceInitialiser_GUI juce;
  const String spikeBundle = argv[1], iplug2Bundle = argv[3];
  const File fixtures(argv[2]);
  std::printf("spike_host %s\n", spikeBundle.toRawUTF8());

  AudioPluginFormatManager formats;
  formats.addFormat(std::make_unique<VST3PluginFormat>());

  CheckClass(File(spikeBundle), fixtures);
  if (auto spike = Load(formats, spikeBundle))
    CheckParameters(*spike, fixtures);
  else
    Check(false, "the spike loads", spikeBundle);
  for (const char* scenario : {"default", "slots", "bypassed"})
    CheckFixture(formats, spikeBundle, iplug2Bundle, fixtures, scenario);
  CheckSound(formats, spikeBundle, iplug2Bundle, fixtures);

  std::printf("%s\n", gFails ? "FAIL" : "ok");
  return gFails ? 1 : 0;
}
