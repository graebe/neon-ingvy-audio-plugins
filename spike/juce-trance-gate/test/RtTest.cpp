/*
 * The spike's audio callback allocates nothing.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Torben Gräber
 *
 *   spike_rt <tests/fixtures/iplug2>      (run with alloc_guard inserted)
 *
 * The processor itself, as its VST3 wrapper drives it, under alloc_guard:
 * every malloc the calling thread makes inside processBlock is counted --
 * the shell's C++, JUCE's, and the Rust engine's alike. The blocks cover
 * what a session does to it: a set loaded (the engine applies it at the top
 * of a block), steady playback under a running transport, the host moving
 * Slot (the switch, and the follow it publishes), and a second load. Each
 * must count zero.
 *
 * The guard is shown an allocation first, so a guard that was not inserted
 * -- and would count nothing ever -- fails rather than passes.
 */
#include "Processor.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

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

/* Where the guard's own check lets an allocation be seen. */
std::vector<float>* volatile gEscape = nullptr;

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;

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
    p.setPpqPosition(double(sample) / kSampleRate * 2.0);
    return p;
  }
};

} // namespace

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::fprintf(stderr, "usage: spike_rt <fixtures-dir>\n");
    return 2;
  }
  const ScopedJuceInitialiser_GUI juce;
  const File fixtures = File(argv[1]).getChildFile("NITranceGate");
  std::printf("spike_rt\n");

  const auto begin = reinterpret_cast<void (*)()>(dlsym(RTLD_DEFAULT, "ni_alloc_watch_begin"));
  const auto end = reinterpret_cast<long (*)()>(dlsym(RTLD_DEFAULT, "ni_alloc_watch_end"));
  Check(begin && end, "the allocation guard is inserted");
  if (!begin || !end)
    return 1;
  begin();
  void* volatile shown = std::malloc(64);
  std::free(shown);
  Check(end() >= 1, "... and counts a malloc it is shown");
  begin();
  {
    /* Seen from outside, or the compiler may elide the allocation itself. */
    std::vector<float> grown(1000);
    gEscape = &grown;
  }
  gEscape = nullptr;
  Check(end() >= 1, "... and a std::vector's, through the system's operator new");

  ni::tg::Processor processor;
  Transport transport;
  processor.setPlayHead(&transport);
  processor.prepareToPlay(kSampleRate, kBlock);

  AudioBuffer<float> buffer(2, kBlock);
  MidiBuffer midi;
  long allocations = 0;
  const auto play = [&](int blocks) {
    for (int b = 0; b < blocks; b++)
    {
      for (int ch = 0; ch < 2; ch++)
        for (int i = 0; i < kBlock; i++)
          buffer.setSample(ch, i, 0.5f);
      begin();
      processor.processBlock(buffer, midi);
      allocations += end();
      transport.sample += kBlock;
    }
  };
  const auto load = [&](const char* scenario) {
    MemoryBlock state;
    fixtures.getChildFile(String(scenario) + ".component.bin").loadFileAsData(state);
    processor.setStateInformation(state.getData(), int(state.getSize()));
  };

  load("slots");
  play(200);
  Check(allocations == 0, "a loaded set, then two seconds of playback", String(allocations));

  allocations = 0;
  auto* slot = dynamic_cast<RangedAudioParameter*>(processor.getParameters()[ni::tg::kSlot]);
  Check(slot != nullptr, "Slot is a ranged parameter");
  if (slot)
    slot->setValueNotifyingHost(slot->convertTo0to1(3.0f));
  play(20);
  Check(allocations == 0, "the host moves Slot: the switch and its follow", String(allocations));

  allocations = 0;
  load("default");
  play(20);
  Check(allocations == 0, "a second set loaded while playing", String(allocations));

  processor.releaseResources();
  processor.setPlayHead(nullptr);
  std::printf("%s\n", gFails ? "FAIL" : "ok");
  return gFails ? 1 : 0;
}
