// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber
/*
 * clocksync.js -- an engine's playhead, in this page's time. Pure.
 *
 * The same two pieces as Piano Practice's (graebe/schwung-piano-practice,
 * web/src/session/clocksync.js), which has the whole argument; kept the same.
 *
 * A Move module's readout reaches its page anything from ~20 ms to ~400 ms
 * after it was true -- the manager's 150 ms re-read and 500 ms heartbeat, the
 * shim, WiFi, the iframe -- and every one by a different amount. A page that
 * carries the playhead from a readout's ARRIVAL draws it pulled back by that
 * delay, a different amount each time. So each engine stamps its `ui` readout
 * with its own clock (`devms`: the time it had processed), and:
 *
 *   createClockSync()    the offset between the engine's clock and the page's,
 *                        from the SMALLEST delay seen (a readout is late, never
 *                        early), over the last 64 readouts and 30 s
 *   createPlayhead()     the position to draw: the readout's, carried from ITS
 *                        time at its rate; a correction eased in over ~100 ms;
 *                        a moving playhead never stepping back; a jump of
 *                        `snapBeats` or more (a restart, a wrap) drawn at once
 *
 * "Beats" are whatever unit the position is in: steps for the Trance Gate,
 * cycles of the sweep for the Side-Chain. lib/clock.js puts these behind
 * createClock().
 */

export const SYNC_SAMPLES = 64;
export const SYNC_SPAN_MS = 30000;
/* How long an eased correction takes to fade to a third. */
export const EASE_MS = 100;
/* A difference this large, in beats, is a jump to draw at once. */
export const SNAP_BEATS = 1;

export function createClockSync({ samples = SYNC_SAMPLES, spanMs = SYNC_SPAN_MS } = {}) {
  const seen = [];          /* [{ local, offset }] oldest first */
  let best = null;

  function recompute() {
    best = null;
    for (const s of seen) if (best === null || s.offset < best) best = s.offset;
  }

  return {
    observe(t, local) {
      if (!Number.isFinite(t) || !Number.isFinite(local)) return;
      seen.push({ local, offset: local - t });
      while (seen.length > samples || (seen.length > 1 && local - seen[0].local > spanMs)) seen.shift();
      recompute();
    },
    deviceNow: (local) => (best === null ? null : local - best),
    get offset() { return best; },
  };
}

export function createPlayhead({ sync = createClockSync(), easeMs = EASE_MS, snapBeats = SNAP_BEATS } = {}) {
  let c = null;             /* the latest picture's clock */
  let arrivedAt = 0;        /* when it arrived, for a picture without t */
  let ease = 0;             /* the correction still being eased out, in beats */
  let easeFrom = 0;
  let shown = null;         /* the beat drawn last */

  const moving = (k) => !!k && k.running && !k.paused && !k.blocked && k.perBeatMs > 0;

  /* The beat the Move freezes on, or Infinity. Never behind the picture's own
   * beat: a picture already past it (a stale waitAt) is not pulled back. */
  const stopAt = () => (c.waitAt === null || c.waitAt === undefined ? Infinity : Math.max(c.beats, c.waitAt));

  /* Where the beat truly is at `local`, by the latest picture. */
  function target(local) {
    if (!c) return 0;
    let b = c.beats;
    if (moving(c)) {
      const dev = c.t === null || c.t === undefined ? null : sync.deviceNow(local);
      const elapsed = dev === null ? local - arrivedAt : dev - c.t;
      b += Math.max(0, elapsed) / c.perBeatMs;
    }
    return Math.min(b, stopAt());
  }

  function at(local) {
    if (!c) return null;
    let b = target(local) + ease * Math.exp(-(local - easeFrom) / easeMs);
    /* Moving, the playhead never steps back: it holds until the beat passes. */
    if (moving(c) && shown !== null && b < shown) b = shown;
    b = Math.min(b, stopAt());
    shown = b;
    return b;
  }

  return {
    anchor(clock, local) {
      if (!clock) return;
      if (Number.isFinite(clock.t)) sync.observe(clock.t, local);
      const was = c && shown !== null ? at(local) : null;
      c = clock;
      arrivedAt = local;
      const now = target(local);
      const diff = was === null ? Infinity : was - now;
      if (!moving(c) || Math.abs(diff) >= snapBeats) {
        ease = 0;
        shown = null;
      } else {
        ease = diff;
        easeFrom = local;
      }
    },
    at,
    sync,
  };
}
