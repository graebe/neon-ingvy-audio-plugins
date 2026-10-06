// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The audio thread's flush-to-zero guard: on for the block, and the host's mode
 * back afterwards.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "shell_denormals.h"

#include <cfloat>

namespace {

/* Through volatiles, so the compiler cannot fold it at build time -- where
 * the FPU's mode does not apply. */
double halve(double x)
{
  volatile double v = x;
  volatile double h = 0.5;
  return v * h;
}

float scale(float x)
{
  volatile float v = x;
  volatile float s = 0.25f;
  return v * s;
}

} // namespace

TEST_CASE("a denormal result is flushed inside the guard, and not outside it")
{
  const double tiny = DBL_MIN; /* the smallest normal: half of it is denormal */
  CHECK(halve(tiny) != 0.0);
  {
    const shell::ScopedFlushDenormals ftz;
    CHECK(halve(tiny) == 0.0);
    CHECK(scale(FLT_MIN) == 0.0f);
  }
  CHECK(halve(tiny) != 0.0);
  CHECK(scale(FLT_MIN) != 0.0f);
}

TEST_CASE("the mode the host had is the mode it gets back, whatever it was")
{
  using G = shell::ScopedFlushDenormals;
  const G::Word before = G::Read();
  {
    const G outer;
    {
      const G inner;
      CHECK((G::Read() & G::kBits) == G::kBits);
    }
    /* The inner guard restored the outer one's mode, not the host's. */
    CHECK((G::Read() & G::kBits) == G::kBits);
  }
  CHECK(G::Read() == before);

  /* A host that already runs flushed keeps running flushed. */
  G::Write(before | G::kBits);
  {
    const G ftz;
  }
  CHECK(G::Read() == (before | G::kBits));
  G::Write(before);
}

TEST_CASE("a denormal operand counts as zero inside the guard")
{
  /* DAZ on x86, and FZ covers inputs as well on arm64. */
  const double denormal = halve(DBL_MIN);
  REQUIRE(denormal != 0.0);
  const shell::ScopedFlushDenormals ftz;
  volatile double d = denormal;
  volatile double big = 1e300;
  CHECK(d * big == 0.0);
}
