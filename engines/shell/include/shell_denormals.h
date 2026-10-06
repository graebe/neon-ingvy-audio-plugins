// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * shell_denormals.h -- no denormals on the audio thread, for one block.
 *
 * A filter or an envelope decaying towards silence passes through the
 * denormal range on its way to zero, and on both architectures this runs on a
 * denormal operand can cost tens to hundreds of cycles where a normal one costs
 * one. clap-validator measured the Spectrogram 2.5 to 2.8 times slower on
 * denormal input. The fix every audio host expects a plugin to make is to put
 * the FPU in flush-to-zero mode for the duration of its callback -- and only
 * for that: the host's own thread state is restored on the way out, because
 * the thread is the host's.
 *
 *   arm64   FPCR.FZ (bit 24): denormal inputs and results become zero
 *   x86-64  MXCSR FTZ (bit 15) and DAZ (bit 6), the same pair in two bits
 *
 * Everything the block calls runs under it, the Rust engines included -- the
 * mode is the thread's, not the language's.
 *
 *   void Plugin::ProcessBlock(...)
 *   {
 *     const shell::ScopedFlushDenormals ftz;
 *     ...
 *   }
 *
 * Header-only, allocation-free and wait-free: one register read and two
 * writes per block.
 */
#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define SHELL_FTZ_X86 1
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define SHELL_FTZ_ARM64 1
#else
#error "shell_denormals.h: no flush-to-zero control for this architecture yet"
#endif

namespace shell {

class ScopedFlushDenormals
{
public:
  ScopedFlushDenormals() noexcept : mSaved(Read()) { Write(mSaved | kBits); }
  ~ScopedFlushDenormals() noexcept { Write(mSaved); }

  ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
  ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

#if SHELL_FTZ_X86
  using Word = unsigned int;
  static constexpr Word kBits = 0x8040u; /* FTZ | DAZ */
  static Word Read() noexcept { return _mm_getcsr(); }
  static void Write(Word w) noexcept { _mm_setcsr(w); }
#else
  using Word = uint64_t;
  static constexpr Word kBits = Word(1) << 24; /* FPCR.FZ */
  static Word Read() noexcept
  {
    Word w;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(w));
    return w;
  }
  static void Write(Word w) noexcept { __asm__ __volatile__("msr fpcr, %0" : : "r"(w)); }
#endif

private:
  const Word mSaved;
};

} // namespace shell
