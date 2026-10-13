// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The ground's rings, from a playhead. Pure. Shared by the Move's editors.
 *
 * In Live the window's ground rings on the host's beat: the plugin watches the
 * transport and sends one ring a beat, full strength on a bar's downbeat. On
 * the Move no audio and no beat clock reach the page -- only the engine's
 * playhead, a couple of times a second, which the editor carries forward on
 * its clock. So the beats are found in that position instead:
 *
 *   a beat is a quarter note, which the Rate says how many steps make
 *   ("1/16" is four, "1/8T" three, "1/1T" three eighths); the beats are
 *   counted from the cycle's first step, and that step rings full -- the
 *   cycle is what the window shows, where Live's ring marks the host's bar,
 *   which the page is not told. The Trance Gate's cycle is its pattern, a
 *   step each; the Side-Chain's is one step of its Rate.
 *
 * Ringing is FORWARD MOTION ONLY. A new anchor from the engine can land a
 * little behind the position the clock had carried forward to; that is the
 * playhead being corrected, not the music going back, and must not ring a
 * beat it already rang.
 */

/** Quarter notes per step for a Rate label, or null for no label. */
export function beatsPerStep(label) {
  const m = /^1\/(\d+)(T?)$/.exec(String(label ?? '').trim());
  if (!m) return null;
  const whole = 4 / Number(m[1]);
  return m[2] ? whole * 2 / 3 : whole;
}

/* How far back an anchor may land and still count as a correction: a quarter
 * of the cycle, and at most two steps. */
const SLACK = (length) => Math.min(2, length / 4);

/**
 * A ring detector. `next(pos, length, label)` takes the playhead -- a position
 * in steps, wrapped to the pattern -- and returns the ring it crossed since the
 * last call (1 at the cycle's start, 0.4 on another beat) or null. `reset()`
 * forgets where it was: call it when the transport stops.
 */
export function createBeats() {
  let prev = null;
  return {
    reset() { prev = null; },
    next(pos, length, label) {
      const n = Math.max(1, length | 0);
      const bps = beatsPerStep(label);
      if (!(bps > 0) || !Number.isFinite(pos)) { prev = null; return null; }
      const p = ((pos % n) + n) % n;
      if (prev === null) { prev = p; return null; }
      let d = p - prev;
      if (d < 0) d += n;
      /* Behind by a little: a correction; hold the old place. */
      if (d > n - SLACK(n)) return null;
      const from = prev;
      prev = p;
      const to = from + d;
      if (to >= n) return 1;
      const spb = 1 / bps;
      /* A beat between `from` (exclusive) and `to` (inclusive), counted from
       * step 0 of the cycle. */
      return Math.floor(to / spb) > Math.floor(from / spb) ? 0.4 : null;
    },
  };
}
