/*
 * shell_state.h -- the header every plugin's state chunk starts with.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 *   magic   8 bytes   'N' 'I' 's' 't' 00 00 F8 7F
 *   version int32     the plugin's own chunk version
 *   size    int32     bytes of body that follow
 *   body              what the plugin wrote: parameters first, then its own
 *
 * WHY NO OLDER CHUNK CAN LOOK LIKE ONE. Every chunk written before this header
 * existed starts with iPlug2's parameter block, whose first eight bytes are the
 * first parameter's value as a double -- and a parameter is clamped to its
 * range, so it is never NaN. Read as a little-endian double, the magic is a
 * quiet NaN. A plugin with no parameters (the Spectrogram) started with an
 * IByteChunk string instead, whose first four bytes are its length -- and
 * 'NIst' read as that length is 1.9 GB. So no header means the old layout,
 * which is what lets every project saved before this still open.
 *
 * WHY A SIZE. A later version may append fields this build does not know. The
 * size lets it skip them and return the position just past the body, which a
 * VST3 host needs to find the bypass flag iPlug2 writes after the chunk.
 *
 * Header-only and templated on the chunk, so the tests can drive it with the
 * same iplug::IByteChunk the plugin uses.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace shell {
namespace state {

constexpr uint8_t kMagic[8] = {'N', 'I', 's', 't', 0x00, 0x00, 0xF8, 0x7F};
constexpr int kHeaderBytes = 16;

struct Header
{
  bool legacy = true;     /* no header: the body is the whole chunk from startPos */
  int32_t version = 0;    /* 0 for a legacy chunk */
  int body = 0;           /* where the parameters start */
  int end = -1;           /* one past the body; -1 for a legacy chunk */
};

/* Write the header, returning where the size goes for End. */
template <class Chunk>
int Begin(Chunk& chunk, int32_t version)
{
  chunk.PutBytes(kMagic, int(sizeof kMagic));
  chunk.Put(&version);
  const int at = chunk.Size();
  const int32_t size = 0;
  chunk.Put(&size);
  return at;
}

/* Fill in the size, once the body is written. */
template <class Chunk>
bool End(Chunk& chunk, int at)
{
  const int32_t size = int32_t(chunk.Size() - (at + int(sizeof(int32_t))));
  if (at < 0 || size < 0) return false;
  std::memcpy(chunk.GetData() + at, &size, sizeof size);
  return true;
}

/*
 * Read the header at startPos. A chunk without one is legacy, and its body
 * starts at startPos; a headed chunk whose size runs past the end is refused
 * as legacy would be misread, so it reports body = -1.
 *
 * NOTHING AT ALL IS REFUSED TOO. Every plugin here writes at least the header,
 * and every older build wrote at least its parameters -- except the
 * Spectrogram's first, which had none and wrote zero bytes. That chunk
 * restores nothing: refusing it leaves the instance exactly as loading it did.
 * The AU wrapper already turned it down (a zero-length restore reads as a
 * failure there), and a CLAP host is entitled to hear the same.
 */
template <class Chunk>
Header Read(const Chunk& chunk, int startPos)
{
  Header h;
  h.body = startPos;
  if (startPos < 0 || startPos >= chunk.Size())
  {
    h.body = -1;
    return h;
  }
  uint8_t magic[8];
  if (chunk.GetBytes(magic, int(sizeof magic), startPos) < 0 ||
      std::memcmp(magic, kMagic, sizeof magic) != 0)
    return h;

  int32_t version = 0, size = 0;
  int pos = chunk.Get(&version, startPos + int(sizeof magic));
  if (pos >= 0) pos = chunk.Get(&size, pos);
  h.legacy = false;
  h.version = version;
  if (pos < 0 || size < 0 || pos + size > chunk.Size())
  {
    h.body = -1;
    return h;
  }
  h.body = pos;
  h.end = pos + size;
  return h;
}

/*
 * THE PARAMETER BLOCK, CHECKED BEFORE ANY OF IT IS APPLIED.
 *
 * iPlug2's UnserializeParams sets each value as it reads it and clamps what it
 * sets, so it accepts any bytes at all -- random data included -- and leaves
 * the instance half overwritten when the block turns out to be short. This
 * reads the block first and says whether it could be one SerializeParams
 * wrote: a double per parameter, every one finite, and a stepped parameter's
 * (int, enum, bool) a whole number, because Constrain rounds those to their
 * step before they are ever stored.
 *
 * NOT A RANGE CHECK, deliberately. A range can narrow between versions -- the
 * Side-Chain's Curve lost an option -- and an older project holding the value
 * that went must still open, clamped, as it always has. Being a number of the
 * right kind is what every build has always written.
 *
 * `plug` is anything with NParams() and GetParam(i)->Type(): the plugin, or a
 * test's stand-in. Returns the position after the block, or -1.
 */
template <class Chunk, class Plugin>
int CheckParams(const Chunk& chunk, int pos, const Plugin& plug)
{
  for (int i = 0; i < plug.NParams() && pos >= 0; i++)
  {
    double v = 0.0;
    pos = chunk.Get(&v, pos);
    if (pos < 0 || !std::isfinite(v))
      return -1;
    const auto* p = plug.GetParam(i);
    const bool whole = p->Type() != std::decay_t<decltype(*p)>::kTypeDouble;
    if (whole && !(std::fabs(v) <= 2147483647.0 && v == std::floor(v)))
      return -1;
  }
  return pos;
}

/* The block checked, then applied: the plugin's UnserializeParams runs only on
 * one that CheckParams accepted, so a refused chunk changes nothing. */
template <class Chunk, class Plugin>
int LoadParams(Plugin& plug, const Chunk& chunk, int pos)
{
  if (CheckParams(chunk, pos, plug) < 0)
    return -1;
  return plug.UnserializeParams(chunk, pos);
}

/*
 * ONE STRING FROM THE CHUNK, OR -1.
 *
 * IByteChunk::GetStr reads a length and returns the position past that many
 * bytes WITHOUT checking they are there: a length running off the end returns
 * a position past the end (and leaves the string untouched), a negative one a
 * position before the string. Garbage therefore "reads" -- which is how random
 * bytes got past every plugin's string fields. This is the same read with the
 * two checks it lacks.
 */
template <class Chunk, class String>
int GetStr(const Chunk& chunk, String& str, int pos)
{
  if (pos < 0)
    return -1;
  const int after = chunk.GetStr(str, pos);
  if (after < pos + int(sizeof(int32_t)) || after > chunk.Size())
    return -1;
  return after;
}

/* Where the plugin's UnserializeState returns: past the body for a headed
 * chunk, wherever its own reading stopped for a legacy one. */
inline int Finish(const Header& h, int pos)
{
  return h.legacy ? pos : h.end;
}

} // namespace state
} // namespace shell
