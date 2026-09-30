/*
 * NI Listen-In -- the parameter and the state chunk. See State.h.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "State.h"
#include "Wire.h"
#include "audio_bus.h"
#include "shell_state.h"

namespace listenin {
namespace state {

void Declare(const std::function<iplug::IParam*(int)>& param)
{
  /* The engine's own limit, not a copy of it. */
  param(kSlot)->InitInt("Bus", 1, 1, int(abus_max_slot()), "");
}

/*
 * THE STATE CHUNK: parameters, then the label.
 *
 * The slot is a parameter and SerializeParams handles it. The name is text and
 * cannot be a parameter, so it is appended.
 */
bool Save(iplug::IByteChunk& chunk, const PutParams& params, const std::string& label)
{
  const int at = shell::state::Begin(chunk, kChunkVersion);
  if (!params(chunk))
    return false;
  if (chunk.PutStr(label.c_str()) <= 0)
    return false;
  return shell::state::End(chunk, at);
}

/*
 * THE LABEL IS NOT OPTIONAL. Every build has written it after the parameter,
 * if only as an empty string, so a chunk without one is not a chunk this plugin
 * wrote. Fields a later version appends after it are skipped by the header's
 * size, which is where Finish returns.
 */
int Load(const iplug::IByteChunk& chunk, int startPos, const GetParams& check,
         const GetParams& apply, std::string& label)
{
  const shell::state::Header h = shell::state::Read(chunk, startPos);
  if (h.body < 0)
    return -1;
  const int pos = check(chunk, h.body);
  if (pos < 0)
    return -1;
  WDL_String text;
  const int after = shell::state::GetStr(chunk, text, pos);
  if (after < pos)
    return -1;

  if (apply(chunk, h.body) != pos)
    return -1;
  char clean[32];
  wire::parse_label(text.Get(), clean, int(sizeof(clean)));
  label = clean;
  return shell::state::Finish(h, after);
}

std::string Session::Label() const
{
  std::lock_guard<std::mutex> hold(mLock);
  return mLabel;
}

int Session::Load(const iplug::IByteChunk& chunk, int startPos, const GetParams& check,
                  const GetParams& apply)
{
  std::string label;
  const int pos = state::Load(chunk, startPos, check, apply, label);
  if (pos < 0)
    return -1;
  std::lock_guard<std::mutex> hold(mLock);
  mLabel = std::move(label);
  mChanged = true;
  mLoaded = true;
  return pos;
}

void Session::Edit(const std::string& label)
{
  std::lock_guard<std::mutex> hold(mLock);
  mLabel = label;
  mChanged = true;
}

bool Session::Take(std::string& label, bool& loaded)
{
  std::lock_guard<std::mutex> hold(mLock);
  loaded = mLoaded;
  if (!mChanged)
    return false;
  /* Copied under the lock; the caller hands it to the bus after. */
  label = mLabel;
  mChanged = false;
  mLoaded = false;
  return true;
}

} // namespace state
} // namespace listenin
