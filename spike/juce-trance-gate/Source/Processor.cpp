// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Trance Gate on JUCE, the spike. See Processor.h.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Torben Gräber
 */
#include "Processor.h"
#include "Chunk.h"
#include "ni/Wire.h"

#include <string>

namespace ni::tg {

namespace {

/* iPlug2's VST3 ID for its bypass parameter (IPlugConstants.h kBypassParam). */
constexpr uint32_t kIPlug2BypassId = 65536;

/* "100.00 %": the iPlug2 build's "%.2f %%", but with '.' whatever the host's
 * locale (ni/Wire.h says why printf cannot be trusted with that). */
juce::String PercentText(float value, int)
{
  std::string s;
  ni::wire::append_fixed(s, value, 2);
  return s + " %";
}

float PercentValue(const juce::String& text)
{
  return float(ni::wire::parse_number(text.toStdString()));
}

juce::StringArray Texts(const Spec& spec)
{
  juce::StringArray out;
  for (int i = 0; i < spec.numTexts; i++)
  {
    if (spec.texts)
    {
      out.add(spec.texts[i]);
      continue;
    }
    /* Rate: the engine's own table. A host stores the INDEX, so a list that
     * disagreed with the engine by one entry would re-point every lane. */
    char label[32];
    out.add(tg_core_rate_label(i, label, int(sizeof label)) > 0 ? juce::String(label) : juce::String(i));
  }
  return out;
}

/*
 * AN INTEGER THE HOST STEPS THROUGH, as iPlug2 declared Slot and Length (VST3
 * stepCount 7 and 127). JUCE's int parameter calls itself continuous unless
 * it says otherwise, and the VST3 wrapper then reports stepCount 0 -- Live
 * would draw a Slot lane as a ramp between slots.
 */
class SteppedInt final : public juce::AudioParameterInt
{
public:
  using AudioParameterInt::AudioParameterInt;
  bool isDiscrete() const override { return true; }
};

/*
 * ONE iPlug2 PARAMETER AS ITS JUCE EQUIVALENT. A percentage is continuous, as
 * iPlug2's InitDouble left it (no stepped flag, VST3 stepCount 0): a value a
 * host automates or a chunk carries is kept as it comes, not snapped.
 */
std::unique_ptr<juce::RangedAudioParameter> Make(const Spec& spec)
{
  const juce::ParameterID id{spec.id, 1};
  switch (spec.kind)
  {
    case Kind::Int:
      return std::make_unique<SteppedInt>(
        id, spec.name, int(spec.min), int(spec.max), int(spec.def),
        juce::AudioParameterIntAttributes().withLabel(spec.label));
    case Kind::Choice:
      return std::make_unique<juce::AudioParameterChoice>(id, spec.name, Texts(spec), int(spec.def));
    case Kind::Bool:
    {
      const juce::StringArray texts = Texts(spec);
      return std::make_unique<juce::AudioParameterBool>(
        id, spec.name, spec.def != 0.0,
        juce::AudioParameterBoolAttributes()
          .withStringFromValueFunction([texts](bool on, int) { return texts[on ? 1 : 0]; })
          .withValueFromStringFunction([texts](const juce::String& text) {
            return text.trim().equalsIgnoreCase(texts[1]) || text.getIntValue() != 0;
          }));
    }
    case Kind::Percent:
      return std::make_unique<juce::AudioParameterFloat>(
        id, spec.name, juce::NormalisableRange<float>(float(spec.min), float(spec.max)), float(spec.def),
        juce::AudioParameterFloatAttributes()
          .withLabel(spec.label)
          .withStringFromValueFunction(PercentText)
          .withValueFromStringFunction(PercentValue));
  }
  return {};
}

} // namespace

Processor::Processor()
: AudioProcessor(BusesProperties()
                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
                   .withOutput("Output", juce::AudioChannelSet::stereo(), true))
, mShell(tg_shell_create(44100.0))
{
  /* In index order: with JUCE_FORCE_USE_LEGACY_PARAM_IDS the index is the
   * VST3 ID, and these have to be iPlug2's IDs. */
  for (int i = 0; i < kNumParams; i++)
  {
    auto param = Make(SpecOf(i));
    mParams[size_t(i)] = param.get();
    addParameter(param.release());
  }
  /* LAST, so its index -- and VST3 ID -- is 15. "off" and "on" as iPlug2's
   * StringListParameter spells them. */
  auto bypass = std::make_unique<juce::AudioParameterBool>(
    juce::ParameterID{"bypass", 1}, "Bypass", false,
    juce::AudioParameterBoolAttributes()
      .withStringFromValueFunction([](bool on, int) { return on ? "on" : "off"; })
      .withValueFromStringFunction([](const juce::String& text) {
        return text.trim().equalsIgnoreCase("on") || text.getIntValue() != 0;
      }));
  mBypass = bypass.get();
  addParameter(bypass.release());

  /* The slot follow (timerCallback): a host's idle rate, as iPlug2's was. */
  startTimerHz(30);
}

Processor::~Processor()
{
  stopTimer();
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
  return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
         layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Processor::prepareToPlay(double sampleRate, int)
{
  tg_shell_post_sample_rate(mShell.get(), sampleRate);
}

double Processor::Plain(int index) const
{
  const auto* param = mParams[size_t(index)];
  return double(param->convertFrom0to1(param->getValue()));
}

void Processor::EngineValues(double (&out)[kNumParams]) const
{
  for (int i = 0; i < kNumParams; i++)
    out[i] = ToEngine(i, Plain(i));
}

/*
 * THE iPlug2 SHELL'S BLOCK (TranceGate::ProcessAudio), on JUCE's float buffers
 * in place: every edit posted since the last block lands at begin, the host's
 * values are pushed over it, the transport and meter go in, and the engine
 * gates the two channels.
 */
void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
  const juce::ScopedNoDenormals noDenormals;
  const int frames = buffer.getNumSamples();
  if (frames <= 0 || buffer.getNumChannels() < 2)
    return;

  double values[kNumParams];
  EngineValues(values);
  tg_core_t* core = tg_shell_begin(mShell.get());
  tg_shell_push(mShell.get(), core, values, kNumParams);

  bool running = false;
  double bpm = 0.0, ppq = -1.0;
  int num = 4, den = 4;
  if (auto* head = getPlayHead())
  {
    if (const auto position = head->getPosition())
    {
      running = position->getIsPlaying();
      if (const auto b = position->getBpm())
        bpm = *b;
      if (const auto p = position->getPpqPosition())
        ppq = *p;
      if (const auto sig = position->getTimeSignature())
      {
        num = sig->numerator;
        den = sig->denominator;
      }
    }
  }
  /* What a missing tempo or position means is decided once, for every shell. */
  const ni::wire::Transport host = ni::wire::host_transport(running, bpm, ppq);
  const tg_transport_t transport = {host.running, host.beats, host.bpm};
  tg_core_set_meter(core, num, den);
  tg_core_process_f32_split(core, buffer.getWritePointer(0), buffer.getWritePointer(1), frames, &transport);
  tg_shell_end(mShell.get(), frames);
}

/*
 * A SLOT SWITCH RECALLS A WHOLE SOUND, and the host's parameters follow it --
 * automation lanes, the host's own panel and the editor all show the recalled
 * values (Patch.cpp's Follow). Slot is the host's own and never set.
 */
void Processor::timerCallback()
{
  double now[kNumParams];
  if (!tg_shell_take_params(mShell.get(), now, kNumParams))
    return;
  for (int i = 0; i < kNumParams; i++)
  {
    if (i == kSlot || SameInEngine(i, Plain(i), now[i]))
      continue;
    auto* param = mParams[size_t(i)];
    param->beginChangeGesture();
    param->setValueNotifyingHost(param->convertTo0to1(float(FromEngine(i, now[i]))));
    param->endChangeGesture();
  }
}

juce::AudioProcessorEditor* Processor::createEditor()
{
  return new juce::GenericAudioProcessorEditor(*this);
}

/*
 * THE CHUNK THE iPlug2 BUILD WROTE, from what the engine published: the host's
 * values, and the blob as the next block will hold it, a queued edit included
 * (tg_shell_save) -- then the bypass after it, which iPlug2 needs to load it.
 */
void Processor::getStateInformation(juce::MemoryBlock& destData)
{
  chunk::State state;
  for (int i = 0; i < kNumParams; i++)
    state.params[i] = Plain(i);
  double values[kNumParams];
  EngineValues(values);
  std::string blob(TG_STATE_MAX, '\0');
  const int length = tg_shell_save(mShell.get(), values, kNumParams, blob.data(), int(blob.size()));
  blob.resize(length > 0 ? size_t(length) : 0);
  state.blob = std::move(blob);
  state.bypass = mBypass->get();
  const std::vector<uint8_t> bytes = chunk::Write(state);
  destData.replaceAll(bytes.data(), bytes.size());
}

/*
 * A SET REOPENED, in iPlug2's order (Patch.cpp's Load): every check before
 * anything is applied, so a chunk no build wrote changes nothing; then the
 * parameters, which are the current slot's; then the blob, every slot's
 * pattern and sound, with those parameters winning over its rounded copy of
 * the current slot -- one edit, applied at the top of the next block.
 */
void Processor::setStateInformation(const void* data, int sizeInBytes)
{
  const auto state = chunk::Read(data, size_t(std::max(sizeInBytes, 0)));
  if (!state)
    return;
  for (int i = 0; i < kNumParams; i++)
  {
    auto* param = mParams[size_t(i)];
    param->setValueNotifyingHost(param->convertTo0to1(float(state->params[i])));
  }
  if (state->bypass)
    mBypass->setValueNotifyingHost(*state->bypass ? 1.0f : 0.0f);

  /* An empty blob is a build that saved none: the engine keeps its pattern,
   * and the parameters reach it as a host's edits do, with the next block. */
  if (state->blob.empty())
    return;
  double values[kNumParams];
  EngineValues(values);
  tg_shell_load(mShell.get(), state->blob.c_str(), values, kNumParams);
}

std::map<uint32_t, juce::String> Processor::getCompatibleParameterIds(const juce::VST3Interface::Id& compatibleClass) const
{
  if (compatibleClass != juce::VST3Interface::hexStringToId(NI_IPLUG2_TG_CLASS))
    return {};
  std::map<uint32_t, juce::String> ids;
  for (int i = 0; i < kNumParams; i++)
    ids[uint32_t(i)] = SpecOf(i).id;
  ids[kIPlug2BypassId] = "bypass";
  return ids;
}

} // namespace ni::tg

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
  return new ni::tg::Processor();
}
