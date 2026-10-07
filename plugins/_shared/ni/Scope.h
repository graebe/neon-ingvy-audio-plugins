// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * ni::Scope -- a signal capture for an editor's plot, written on the audio
 * thread and read on the message thread.
 *
 * The x-axis is ONE CYCLE of the engine, not a window of wall time: sample i of
 * a block is filed under column int(sweep[i] * Cols), where the engine supplies
 * the sweep (tg_core_process_f32_split_tap, sc_core_process_f32_split_tap). So
 * the axis stands still and the editor can draw the engine's own shape over it.
 *
 * MIN AND MAX PER COLUMN, never a mean: a transient is a fraction of a column
 * and a mean would report a signal that is not the one playing. The gain is
 * the column's MINIMUM -- the deepest a duck got. A column starts afresh from
 * its first sample, and the column still being filled is published at the end
 * of every block, so the picture is at most one block behind.
 *
 * `seen` holds the generation a column was last written in, so a reader can
 * tell a column the sweep has not reached from one holding silence; Retire()
 * makes every column unseen at once. `head` is the column last written.
 */
#pragma once

#include "ni/Wire.h"

#include <atomic>
#include <cstdint>

namespace ni {

template <int Cols>
class Scope
{
public:
  static constexpr int kCols = Cols;

  /* Every column back to silence and unseen. While no block runs (OnReset):
   * the audio thread's accumulator is reset too. */
  void Clear()
  {
    for (int i = 0; i < Cols; i++)
    {
      mDryLo[i].store(0.f, std::memory_order_relaxed);
      mDryHi[i].store(0.f, std::memory_order_relaxed);
      mWetLo[i].store(0.f, std::memory_order_relaxed);
      mWetHi[i].store(0.f, std::memory_order_relaxed);
      mGain[i].store(1.f, std::memory_order_relaxed);
      mSeen[i].store(0u, std::memory_order_relaxed);
    }
    mCol = -1;
    mHead.store(0, std::memory_order_release);
    Retire();
  }

  /* Every column unseen until the sweep writes it again. Any thread. */
  void Retire() { mGen.fetch_add(1, std::memory_order_relaxed); }

  /* The audio thread: one block. `gain` may be null. */
  void Push(const float* dry, const float* wet, const float* gain, const float* sweep, int frames)
  {
    const uint32_t gen = mGen.load(std::memory_order_relaxed);
    for (int i = 0; i < frames; i++)
    {
      /* The sweep is 0..1 inclusive, so 1.0 would index one past the end. */
      int col = int(sweep[i] * float(Cols));
      col = col < 0 ? 0 : col >= Cols ? Cols - 1 : col;
      const float g = gain ? gain[i] : 1.f;
      if (col != mCol)
      {
        Flush(gen);
        mCol = col;
        mAccDryLo = mAccDryHi = dry[i];
        mAccWetLo = mAccWetHi = wet[i];
        mAccGain = g;
        continue;
      }
      if (dry[i] < mAccDryLo) mAccDryLo = dry[i];
      if (dry[i] > mAccDryHi) mAccDryHi = dry[i];
      if (wet[i] < mAccWetLo) mAccWetLo = wet[i];
      if (wet[i] > mAccWetHi) mAccWetHi = wet[i];
      if (g < mAccGain) mAccGain = g;
    }
    /* The partial column too: without it a sweep parked at its end (a trigger
     * that has not fired again) would never write its last column. */
    Flush(gen);
  }

  /* ---- the reader ---- */

  int Head() const { return mHead.load(std::memory_order_acquire); }

  bool Seen(int col) const
  {
    return mSeen[col].load(std::memory_order_acquire) == mGen.load(std::memory_order_relaxed);
  }

  /* Column `col` as the floats it holds: dry low, dry high, wet low, wet high
   * -- each -1..1 -- and the gain. For a reader that draws them itself (the
   * JUCE editors), where PutColumn encodes them for a page. */
  void ReadColumn(int col, float (&out)[5]) const
  {
    out[0] = mDryLo[col].load(std::memory_order_relaxed);
    out[1] = mDryHi[col].load(std::memory_order_relaxed);
    out[2] = mWetLo[col].load(std::memory_order_relaxed);
    out[3] = mWetHi[col].load(std::memory_order_relaxed);
    out[4] = mGain[col].load(std::memory_order_relaxed);
  }

  /* Column `col` as four raw bytes -- dry low, dry high, wet low, wet high,
   * each bipolar -- and, with `withGain`, a fifth: the gain, unipolar. Raw, not
   * hex: the transport base64-encodes the payload anyway, and hex inside it
   * cost 2.7 times the bytes and a second decode in the editor. */
  char* PutColumn(char* out, int col, bool withGain) const
  {
    *out++ = char(wire::encode_bipolar(mDryLo[col].load(std::memory_order_relaxed)));
    *out++ = char(wire::encode_bipolar(mDryHi[col].load(std::memory_order_relaxed)));
    *out++ = char(wire::encode_bipolar(mWetLo[col].load(std::memory_order_relaxed)));
    *out++ = char(wire::encode_bipolar(mWetHi[col].load(std::memory_order_relaxed)));
    if (withGain)
      *out++ = char(wire::encode_unipolar(mGain[col].load(std::memory_order_relaxed)));
    return out;
  }

private:
  void Flush(uint32_t gen)
  {
    if (mCol < 0)
      return;
    mDryLo[mCol].store(mAccDryLo, std::memory_order_relaxed);
    mDryHi[mCol].store(mAccDryHi, std::memory_order_relaxed);
    mWetLo[mCol].store(mAccWetLo, std::memory_order_relaxed);
    mWetHi[mCol].store(mAccWetHi, std::memory_order_relaxed);
    mGain[mCol].store(mAccGain, std::memory_order_relaxed);
    /* Released last, so a reader that sees the column also sees its values. */
    mSeen[mCol].store(gen, std::memory_order_release);
    mHead.store(mCol, std::memory_order_release);
  }

  std::atomic<float> mDryLo[Cols] = {}, mDryHi[Cols] = {};
  std::atomic<float> mWetLo[Cols] = {}, mWetHi[Cols] = {};
  std::atomic<float> mGain[Cols] = {};
  std::atomic<uint32_t> mSeen[Cols] = {};
  std::atomic<int> mHead{0};
  /* Starts at 1 and only rises, so a column's initial 0 is never live. */
  std::atomic<uint32_t> mGen{1};

  /* The audio thread's own: the column being filled and its running bounds. */
  int mCol = -1;
  float mAccDryLo = 0.f, mAccDryHi = 0.f, mAccWetLo = 0.f, mAccWetHi = 0.f, mAccGain = 1.f;
};

} // namespace ni
