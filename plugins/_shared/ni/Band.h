// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * ni::Band -- one more signal for a plot whose x-axis is one cycle, written
 * on the audio thread and read on the message thread: ni::Scope's columns,
 * min and max, for a signal that arrives on its own schedule.
 *
 * Side-Chain's kick is the case: it comes from another track, a block or so
 * after the ducker's own audio, with the sweep each sample had already worked
 * out (sc_kick_drain). So it cannot ride in the Scope's Push beside the dry
 * and the wet, and gets the same columns of its own. Everything Scope.h says
 * about columns, min and max, `seen` and Retire() holds here.
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace ni {

template <int Cols>
class Band
{
public:
  static constexpr int kCols = Cols;

  /* Every column back to silence and unseen. While no block runs. */
  void Clear()
  {
    for (int i = 0; i < Cols; i++)
    {
      mLo[i].store(0.f, std::memory_order_relaxed);
      mHi[i].store(0.f, std::memory_order_relaxed);
      mSeen[i].store(0u, std::memory_order_relaxed);
    }
    mCol = -1;
    Retire();
  }

  /* Every column unseen until a sample lands in it again. Any thread. */
  void Retire() { mGen.fetch_add(1, std::memory_order_relaxed); }

  /* The audio thread: `frames` samples, each with its sweep 0..1. */
  void Push(const float* x, const float* sweep, int frames)
  {
    const uint32_t gen = mGen.load(std::memory_order_relaxed);
    for (int i = 0; i < frames; i++)
    {
      int col = int(sweep[i] * float(Cols));
      col = col < 0 ? 0 : col >= Cols ? Cols - 1 : col;
      if (col != mCol)
      {
        Flush(gen);
        mCol = col;
        mAccLo = mAccHi = x[i];
        continue;
      }
      if (x[i] < mAccLo) mAccLo = x[i];
      if (x[i] > mAccHi) mAccHi = x[i];
    }
    Flush(gen);
  }

  /* ---- the reader ---- */

  bool Seen(int col) const
  {
    return mSeen[col].load(std::memory_order_acquire) == mGen.load(std::memory_order_relaxed);
  }

  /* Column `col`: low, high, each -1..1. */
  void ReadColumn(int col, float (&out)[2]) const
  {
    out[0] = mLo[col].load(std::memory_order_relaxed);
    out[1] = mHi[col].load(std::memory_order_relaxed);
  }

private:
  void Flush(uint32_t gen)
  {
    if (mCol < 0)
      return;
    mLo[mCol].store(mAccLo, std::memory_order_relaxed);
    mHi[mCol].store(mAccHi, std::memory_order_relaxed);
    /* Released last, so a reader that sees the column also sees its values. */
    mSeen[mCol].store(gen, std::memory_order_release);
  }

  std::atomic<float> mLo[Cols] = {}, mHi[Cols] = {};
  std::atomic<uint32_t> mSeen[Cols] = {};
  /* Starts at 1 and only rises, so a column's initial 0 is never live. */
  std::atomic<uint32_t> mGen{1};

  int mCol = -1;
  float mAccLo = 0.f, mAccHi = 0.f;
};

} // namespace ni
