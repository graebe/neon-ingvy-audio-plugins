// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Spectrogram -- the state chunk. See State.h.
 */
#include "State.h"
#include "Wire.h"
#include "ni/Wire.h"
#include "shell_state.h"

#include <cstdio>
#include <string>

namespace spectro {
namespace state {

/*
 * WHAT A SESSION HAS TO REMEMBER: which buses this window was looking at, and
 * what it was calling a clash.
 *
 * Written as ONE STRING rather than as a count and a loop, because a reader
 * that trusts a count it did not write is a reader that can be made to walk off
 * the end of a chunk. parse_slots already skips anything unreadable.
 */
bool Save(iplug::IByteChunk& chunk, const PutParams& params, const Fields& f)
{
  const int at = shell::state::Begin(chunk, kChunkVersion);
  if (!params(chunk))
    return false;

  std::string sel;
  for (size_t i = 0; i < f.sources.size(); i++)
  {
    if (i)
      sel += ',';
    char num[16];
    snprintf(num, sizeof num, "%u", f.sources[i]);
    sel += num;
  }
  if (chunk.PutStr(sel.c_str()) <= 0)
    return false;

  /* '.' whatever the locale; parse_range reads it back the same way. */
  std::string clash;
  ni::wire::append_fixed(clash, f.clashFloorDb, 2);
  clash += ':';
  ni::wire::append_fixed(clash, f.clashBalanceDb, 2);
  if (chunk.PutStr(clash.c_str()) <= 0)
    return false;

  /* The view and the comparison: what the window was showing, and what it was
   * measuring. Two separate settings, saved separately. */
  std::string view;
  for (size_t i = 0; i < f.view.size(); i++)
  {
    if (i)
      view += ',';
    char num[16];
    snprintf(num, sizeof num, "%d", f.view[i]);
    view += num;
  }
  if (chunk.PutStr(view.c_str()) <= 0)
    return false;

  char cmp[48];
  snprintf(cmp, sizeof cmp, "%d:%d:%d", f.cmpA, f.cmpB, f.clashOn ? 1 : 0);
  if (chunk.PutStr(cmp) <= 0)
    return false;

  /* The zoom, last, so an older build reading this stops before it. */
  std::string range;
  ni::wire::append_fixed(range, f.rangeLo, 2);
  range += ':';
  ni::wire::append_fixed(range, f.rangeHi, 2);
  if (chunk.PutStr(range.c_str()) <= 0)
    return false;
  return shell::state::End(chunk, at);
}

/* What Save writes for the selection: slot numbers and commas, nothing else. */
static bool is_selection(const char* s)
{
  for (; s && *s; s++)
    if (!((*s >= '0' && *s <= '9') || *s == ','))
      return false;
  return true;
}

/*
 * READ BACK DEFENSIVELY, BUT NOT BLINDLY.
 *
 * The chunk has grown a field at a time -- the selection first, then the clash,
 * then the view and the comparison -- so a session saved by an earlier build
 * ends early, and one written by a later build may hold more than this one
 * knows how to want. Every field after the selection is therefore taken only if
 * it is there.
 *
 * THE SELECTION IS NOT OPTIONAL. Every build that wrote anything wrote it, and
 * always as digits and commas, so a chunk it cannot be read from is not one of
 * ours. (The builds before it wrote no bytes at all; shell_state.h's Read
 * refuses that chunk, which holds nothing.)
 */
int Load(const iplug::IByteChunk& chunk, int startPos, const GetParams& check,
         const GetParams& apply, Fields& f)
{
  const shell::state::Header h = shell::state::Read(chunk, startPos);
  if (h.body < 0)
    return -1;
  const int body = check(chunk, h.body);
  if (body < 0)
    return -1;

  WDL_String sel;
  int pos = shell::state::GetStr(chunk, sel, body);
  if (pos < 0 || !is_selection(sel.Get()))
    return -1;
  if (apply(chunk, h.body) != body)
    return -1;
  f.sources.clear();
  spectro::wire::parse_slots(sel.Get(), f.sources);

  WDL_String clash;
  int after = shell::state::GetStr(chunk, clash, pos);
  if (after > pos)
  {
    float floorDb = 0.f, balanceDb = 0.f;
    if (spectro::wire::parse_range(clash.Get(), floorDb, balanceDb))
    {
      f.clashFloorDb = floorDb;
      f.clashBalanceDb = balanceDb;
    }
    pos = after;
  }

  WDL_String view;
  after = shell::state::GetStr(chunk, view, pos);
  if (after > pos)
  {
    f.view.clear();
    spectro::wire::parse_channels(view.Get(), f.view);
    if (f.view.empty())
      f.view.assign(1, 0);
    pos = after;
  }

  WDL_String cmp;
  after = shell::state::GetStr(chunk, cmp, pos);
  if (after > pos)
  {
    int a = 0, b = 0;
    bool on = false;
    if (spectro::wire::parse_compare(cmp.Get(), a, b, on))
    {
      f.cmpA = a;
      f.cmpB = b;
      f.clashOn = on;
    }
    pos = after;
  }

  WDL_String range;
  after = shell::state::GetStr(chunk, range, pos);
  if (after > pos)
  {
    float lo = 0.f, hi = 0.f;
    if (spectro::wire::parse_range(range.Get(), lo, hi) && lo > 0.f && hi > lo)
    {
      f.rangeLo = lo;
      f.rangeHi = hi;
    }
    pos = after;
  }
  return shell::state::Finish(h, pos);
}

Session::Session(const Fields& initial) : mWanted(initial), mApplied(initial) {}

Fields Session::Get() const
{
  std::lock_guard<std::mutex> hold(mLock);
  return mWanted;
}

/* Held across the parse: what the chunk does not carry keeps the value it had
 * at this moment, and an Edit cannot land between the copy and the result. */
int Session::Load(const iplug::IByteChunk& chunk, int startPos, const GetParams& check,
                  const GetParams& apply)
{
  std::lock_guard<std::mutex> hold(mLock);
  Fields f = mWanted;
  const int pos = state::Load(chunk, startPos, check, apply, f);
  if (pos < 0)
    return -1;
  mWanted = std::move(f);
  mChanged = true;
  mLoaded = true;
  return pos;
}

void Session::Edit(const std::function<void(Fields&)>& edit)
{
  std::lock_guard<std::mutex> hold(mLock);
  edit(mWanted);
  mChanged = true;
}

bool Session::Service(Sink& sink, bool all)
{
  Fields next;
  bool loaded = false;
  {
    std::lock_guard<std::mutex> hold(mLock);
    if (!mChanged && !all)
      return false;
    next = mWanted;
    loaded = mLoaded;
    mChanged = false;
    mLoaded = false;
  }
  /* Only what moved: choosing sources waits for the analysis thread, and a
   * view or a comparison is not the receiver's business at all. */
  if (all || next.sources != mApplied.sources)
    sink.ApplySources(next.sources);
  if (all || next.clashFloorDb != mApplied.clashFloorDb ||
      next.clashBalanceDb != mApplied.clashBalanceDb)
    sink.ApplyClash(next.clashFloorDb, next.clashBalanceDb);
  if (all || next.rangeLo != mApplied.rangeLo || next.rangeHi != mApplied.rangeHi)
    sink.ApplyRange(next.rangeLo, next.rangeHi);
  mApplied = std::move(next);
  return loaded;
}

} // namespace state
} // namespace spectro
