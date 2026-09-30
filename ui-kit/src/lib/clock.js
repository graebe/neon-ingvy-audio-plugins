/*
 * The playhead's clock: the engine's position, carried forward at the
 * display's rate. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * OnIdle runs on a main-thread timer -- 50 Hz at best, and worse under a host's
 * UI load -- so drawing the position it reports directly stutters and drifts
 * against the sound. A pushed position is an ANCHOR; between pushes it is
 * interpolated against wall time:
 *
 *     position = pos + (now - at) * perMs
 *
 * One timing source, read at the display's cadence. The Trance Gate and
 * Side-Chain each had a copy, and both kept a requestAnimationFrame loop
 * running forever, ticking nothing while the transport was stopped. This one
 * runs only while the anchor says the position is moving.
 */
import { createSignal, getOwner, onCleanup } from 'solid-js';

/** The interpolated position for an anchor at time `now`. Pure. */
export function positionAt(a, now) {
  if (!a || !a.moving || !(a.perMs > 0)) return a ? a.pos : 0;
  return a.pos + (now - a.at) * a.perMs;
}

/**
 * `set(pos, perMs, moving)` re-anchors; `position()` is reactive and ticks
 * once per frame while moving; `running()` says whether a frame is scheduled.
 * `raf`, `caf` and `now` are injectable for tests.
 */
export function createClock({
  raf = (f) => globalThis.requestAnimationFrame(f),
  caf = (id) => globalThis.cancelAnimationFrame(id),
  now = () => globalThis.performance.now(),
} = {}) {
  const [anchor, setAnchor] = createSignal({ pos: 0, perMs: 0, moving: false, at: 0 });
  const [frame, setFrame] = createSignal(0);
  let id = 0;

  const tick = () => {
    id = 0;
    if (!anchor().moving) return;
    setFrame((n) => n + 1);
    id = raf(tick);
  };
  const stop = () => {
    if (id) caf(id);
    id = 0;
  };

  const set = (pos, perMs, moving) => {
    const live = !!moving && perMs > 0;
    setAnchor({ pos, perMs, moving: live, at: now() });
    if (live && !id) id = raf(tick);
    else if (!live) stop();
  };

  const position = () => {
    const a = anchor();
    if (a.moving) frame();            /* the dependency that makes it tick */
    return positionAt(a, now());
  };

  if (getOwner()) onCleanup(stop);
  return { set, position, anchor, running: () => id !== 0, dispose: stop };
}
