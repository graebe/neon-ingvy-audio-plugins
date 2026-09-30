/*
 * shell_state.h -- the header every plugin's state chunk starts with.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 *   magic   8 bytes   'N' 'I' 's' 't' 00 00 F8 7F
 *   version int32     the plugin's own chunk version
 *   size    int32     bytes of body that follow
 *   body              what the plugin wrote: parameters first, then its own
 *
 * WHY THE MAGIC IS A NaN. Every chunk written before this header existed
 * starts with iPlug2's parameter block, whose first eight bytes are the first
 * parameter's value as a double -- and a parameter is clamped to its range, so
 * it is never NaN. Read as a little-endian double, the magic is a quiet NaN.
 * No legacy chunk can be mistaken for a headed one, which is what lets every
 * project saved before this still open: no header means the old layout.
 *
 * WHY A SIZE. A later version may append fields this build does not know. The
 * size lets it skip them and return the position just past the body, which a
 * VST3 host needs to find the bypass flag iPlug2 writes after the chunk.
 *
 * Header-only and templated on the chunk, so the tests can drive it with the
 * same iplug::IByteChunk the plugin uses.
 */
#pragma once

#include <cstdint>
#include <cstring>

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
 */
template <class Chunk>
Header Read(const Chunk& chunk, int startPos)
{
  Header h;
  h.body = startPos;
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

/* Where the plugin's UnserializeState returns: past the body for a headed
 * chunk, wherever its own reading stopped for a legacy one. */
inline int Finish(const Header& h, int pos)
{
  return h.legacy ? pos : h.end;
}

} // namespace state
} // namespace shell
