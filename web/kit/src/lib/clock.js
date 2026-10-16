// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The playhead's clock: the engine's position, carried forward between the
 * engine's reports.
 *
 * OnIdle runs on a main-thread timer -- 50 Hz at best, and worse under a host's
 * UI load -- so drawing the position it reports directly stutters and drifts
 * against the sound. A pushed position is an ANCHOR; between pushes it is
 * interpolated against wall time:
 *
 *     position = pos + (now - at) * perMs
 *
 * One timing source. The Trance Gate and Side-Chain each had a copy, and both
 * kept a loop running forever, ticking nothing while the transport was
 * stopped. This one runs only while the anchor says the position is moving.
 *
 * A TIMER, NOT THE DISPLAY'S FRAME CALLBACK. A WKWebView that takes its window
 * for hidden stops servicing frame callbacks altogether, and the editor's
 * behaviour may not depend on WebKit's opinion of a host's window (the kit's
 * rule; tests/editor_timing.test.mjs). Every anchor re-renders the position
 * anyway -- the engine reports ~50 times a second -- so the tick is only the
 * interpolation between them; WebKit may throttle it in a page it calls
 * hidden, and the playhead then moves at the engine's rate rather than not at
 * all.
 */
import { createSignal, getOwner, onCleanup } from 'solid-js';
import { createPlayhead } from './clocksync.js';

/** The tick between anchors: about one display frame. */
export const TICK_MS = 16;

/** The interpolated position for an anchor at time `now`. Pure. */
export function positionAt(a, now) {
  if (!a || !a.moving || !(a.perMs > 0)) return a ? a.pos : 0;
  return a.pos + (now - a.at) * a.perMs;
}

/**
 * `set(pos, perMs, moving, devMs, period)` re-anchors; `position()` is reactive and
 * ticks every TICK_MS while moving; `running()` says whether the tick is armed.
 * `every`, `cancel` and `now` are injectable for tests.
 *
 * WITH `devMs` -- the engine's own time when `pos` was true -- the position is
 * carried from THAT time, the engine's clock learnt from the readouts as they
 * arrive, and corrections are eased rather than stepped (lib/clocksync.js):
 * how late a readout arrived no longer moves the playhead. `snap` is the
 * difference, in the position's own unit, that is a jump to draw at once.
 * Without it the position is carried from the readout's arrival, as before.
 * `period` is what the position wraps at, if it does: a readout from after a
 * wrap is then no jump.
 */
export function createClock({
  every = (f, ms) => globalThis.setInterval(f, ms),
  cancel = (id) => globalThis.clearInterval(id),
  now = () => globalThis.performance.now(),
  snap = 1,
} = {}) {
  const [anchor, setAnchor] = createSignal({ pos: 0, perMs: 0, moving: false, at: 0 });
  const [tick, setTick] = createSignal(0);
  let id = 0;

  const stop = () => {
    if (id) cancel(id);
    id = 0;
  };
  /* A tick that was already queued when the transport stopped does nothing. */
  const onTick = () => {
    if (anchor().moving) setTick((n) => n + 1);
  };

  const playhead = createPlayhead({ snapBeats: snap });
  const set = (pos, perMs, moving, devMs, period) => {
    const live = !!moving && perMs > 0;
    const at = now();
    const stamped = Number.isFinite(devMs);
    if (stamped) {
      playhead.anchor({ beats: pos, t: devMs, perBeatMs: perMs > 0 ? 1 / perMs : 0,
                        running: live, paused: false, blocked: false, waitAt: null,
                        period: period > 0 ? period : 0 }, at);
    }
    setAnchor({ pos, perMs, moving: live, at, stamped });
    if (live && !id) id = every(onTick, TICK_MS);
    else if (!live) stop();
  };

  const position = () => {
    const a = anchor();
    if (a.moving) tick();             /* the dependency that makes it tick */
    return a.stamped ? playhead.at(now()) : positionAt(a, now());
  };

  if (getOwner()) onCleanup(stop);
  return { set, position, anchor, running: () => id !== 0, dispose: stop };
}
