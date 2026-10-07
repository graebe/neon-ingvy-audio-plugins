// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Trance Gate's saved state. See Chunk.h and tests/fixtures/iplug2/FORMAT.md.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Torben Gräber
 */
#include "Chunk.h"

#include <cmath>
#include <cstring>
#include <initializer_list>

namespace ni::tg::chunk {

namespace {

/* "NIst", then the tail that makes the eight bytes a quiet NaN when read as a
 * double -- which no parameter can be, so no headerless chunk looks like one
 * (engines/shell/include/shell_state.h). */
constexpr uint8_t kMagic[8] = {'N', 'I', 's', 't', 0x00, 0x00, 0xF8, 0x7F};
constexpr size_t kHeaderBytes = 16;

/*
 * LITTLE-ENDIAN, SPELLED OUT. iPlug2 copied host memory into the chunk, and
 * every machine it shipped on is little-endian, so that is the format; reading
 * and writing byte by byte keeps it the format on any machine this builds for.
 */
int32_t GetI32(const uint8_t* p)
{
  return int32_t(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24);
}

double GetF64(const uint8_t* p)
{
  uint64_t bits = 0;
  for (int i = 7; i >= 0; i--)
    bits = bits << 8 | p[i];
  double v;
  std::memcpy(&v, &bits, sizeof v);
  return v;
}

void PutI32(std::vector<uint8_t>& out, int32_t v)
{
  const auto u = uint32_t(v);
  for (int i = 0; i < 4; i++)
    out.push_back(uint8_t(u >> (8 * i)));
}

void PutF64(std::vector<uint8_t>& out, double v)
{
  uint64_t bits;
  std::memcpy(&bits, &v, sizeof bits);
  for (int i = 0; i < 8; i++)
    out.push_back(uint8_t(bits >> (8 * i)));
}

/*
 * A NUMBER OF THE RIGHT KIND, NOT A NUMBER IN RANGE -- iPlug2's CheckParams.
 * Finite always; a stepped parameter's a whole number, because iPlug2 rounds
 * those before it stores them. A range can narrow between versions, so a
 * value outside it is clamped when applied, never refused.
 */
bool Plausible(int index, double v)
{
  if (!std::isfinite(v))
    return false;
  return !Stepped(index) || (std::fabs(v) <= 2147483647.0 && v == std::floor(v));
}

/*
 * `count` parameters, then the blob string, from `at` and inside `end`. The
 * parameters a chunk does not carry take their defaults. Returns the position
 * just past the blob, or nothing.
 */
std::optional<size_t> GetBody(const uint8_t* b, size_t at, size_t end, int count, State& s)
{
  if (at + size_t(count) * 8 + 4 > end)
    return {};
  for (int i = 0; i < kNumParams; i++)
    s.params[i] = Default(i);
  for (int i = 0; i < count; i++)
  {
    const double v = GetF64(b + at + size_t(i) * 8);
    if (!Plausible(i, v))
      return {};
    s.params[i] = v;
  }
  size_t pos = at + size_t(count) * 8;
  const int32_t len = GetI32(b + pos);
  pos += 4;
  if (len < 0 || pos + size_t(len) > end)
    return {};
  s.blob.assign(reinterpret_cast<const char*>(b + pos), size_t(len));
  s.carried = count;
  return pos + size_t(len);
}

} // namespace

std::optional<State> Read(const void* data, size_t size)
{
  const auto* b = static_cast<const uint8_t*>(data);
  if (b == nullptr || size == 0)
    return {};

  if (size >= kHeaderBytes && std::memcmp(b, kMagic, sizeof kMagic) == 0)
  {
    const int32_t body = GetI32(b + 12);
    if (body < 0 || kHeaderBytes + size_t(body) > size)
      return {};
    const size_t end = kHeaderBytes + size_t(body);
    State s;
    if (!GetBody(b, kHeaderBytes, end, kNumParams, s))
      return {};
    /* Whatever a later version appended lies between the blob and `end`, and
     * is skipped; the wrapper's bypass follows the body. */
    if (size >= end + 4)
      s.bypass = GetI32(b + end) != 0;
    return s;
  }

  /*
   * NO HEADER: A BUILD FROM BEFORE 2026-09-30. Its body starts at 0 and its
   * parameter count is not written down, because it was simply that build's:
   * 15, or 14 or 12 from before Fade Dir and the fade's pair were appended.
   * The count is the one for which the blob string ends exactly at the end of
   * the stream, or four bytes before it, where the wrapper's bypass is. The
   * iPlug2 build itself only ever tried 15, and refused the others.
   */
  for (int count : {15, 14, 12})
  {
    State s;
    const auto after = GetBody(b, 0, size, count, s);
    if (!after)
      continue;
    s.legacy = true;
    if (*after == size)
      return s;
    if (*after + 4 == size)
    {
      s.bypass = GetI32(b + *after) != 0;
      return s;
    }
  }
  return {};
}

std::vector<uint8_t> Write(const State& state)
{
  std::vector<uint8_t> out(kMagic, kMagic + sizeof kMagic);
  PutI32(out, kVersion);
  const size_t sizeAt = out.size();
  PutI32(out, 0);
  for (int i = 0; i < kNumParams; i++)
    PutF64(out, Stepped(i) ? std::round(state.params[i]) : state.params[i]);
  PutI32(out, int32_t(state.blob.size()));
  out.insert(out.end(), state.blob.begin(), state.blob.end());

  const auto body = uint32_t(out.size() - kHeaderBytes);
  for (int i = 0; i < 4; i++)
    out[sizeAt + size_t(i)] = uint8_t(body >> (8 * i));
  /* After the chunk, as iPlug2's VST3 wrapper wrote it: its setState fails
   * when these four bytes are missing. */
  PutI32(out, state.bypass.value_or(false) ? 1 : 0);
  return out;
}

} // namespace ni::tg::chunk
