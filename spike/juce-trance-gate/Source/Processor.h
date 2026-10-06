/*
 * NI Trance Gate on JUCE, the spike: the iPlug2 build's identity, parameters
 * and saved state, around the same Rust engine.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Torben Gräber
 *
 * WHAT IT HAS TO PROVE, before anything of iPlug2 is deleted: that a Live set
 * saved with the iPlug2 VST3 opens in a JUCE build with the same settings.
 * Three things carry a set across, and each has its place here:
 *
 *   the class      the VST3 class ID differs (JUCE derives its own), so the
 *                  build declares the iPlug2 one compatible: CMakeLists.txt's
 *                  JUCE_VST3_COMPATIBLE_CLASSES, which JUCE writes into
 *                  moduleinfo.json and answers through IPluginCompatibility
 *                  -- or, with NI_SPIKE_SAME_CLASS, takes the iPlug2 class ID
 *                  itself (JUCE_VST3_COMPONENT_CLASS);
 *   the parameters the same fifteen at the same IDs (Params.h, with
 *                  JUCE_FORCE_USE_LEGACY_PARAM_IDS), and Bypass, whose ID
 *                  differs, mapped by getCompatibleParameterIds;
 *   the state      the iPlug2 chunk read and written (Chunk.h).
 *
 * Everything else is the minimum that gates audio through the real engine:
 * tg_shell_* as the iPlug2 shell drives it (plugins/trance-gate/TranceGate.cpp)
 * and a generic editor.
 *
 * THREADS. processBlock is the audio thread and touches the engine only
 * between tg_shell_begin and tg_shell_end; nothing there allocates, locks or
 * parses. The state calls and the slot-follow timer run on the message thread
 * and reach the engine through tg_shell's posting side.
 */
#pragma once

#include "Params.h"
#include "tg_shell.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <map>
#include <memory>

namespace ni::tg {

class Processor final : public juce::AudioProcessor,
                        public juce::VST3ClientExtensions,
                        private juce::Timer
{
public:
  Processor();
  ~Processor() override;

  const juce::String getName() const override { return JucePlugin_Name; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }

  /* Stereo in, stereo out, and nothing else: the iPlug2 build's "2-2". */
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}

  /* The iPlug2 chunk, both ways: Chunk.h. */
  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  juce::AudioProcessorParameter* getBypassParameter() const override { return mBypass; }
  juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }

  /*
   * THE iPlug2 BUILD'S PARAMETER IDS, mapped onto these parameters, for a
   * host that reopens its set here (IRemapParamID). 0..14 are the same IDs
   * already; Bypass was 65536 there and is 15 here. Built with
   * NI_SPIKE_SAME_CLASS there is no other class to map from, and a host
   * that finds the old class finds this one.
   */
  std::map<uint32_t, juce::String> getCompatibleParameterIds(const juce::VST3Interface::Id& compatibleClass) const override;

private:
  /* The engine's slot switched; the host's parameters follow it. */
  void timerCallback() override;

  /* A parameter's plain value, in iPlug2's units. Any thread. */
  double Plain(int index) const;
  /* Every parameter on the engine's numeric wire, in tg_param_t order. */
  void EngineValues(double (&out)[kNumParams]) const;

  struct ShellDeleter
  {
    void operator()(tg_shell_t* s) const { tg_shell_destroy(s); }
  };
  std::unique_ptr<tg_shell_t, ShellDeleter> mShell;

  std::array<juce::RangedAudioParameter*, kNumParams> mParams{};
  juce::AudioParameterBool* mBypass = nullptr;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};

} // namespace ni::tg
