// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * At most one message per key per window.
 *
 * A pointer reports far more often than the display draws -- a pad dragged for
 * its amount sent a message on every pointermove, each one a base64 frame
 * across the WebView bridge and an edit queued for the audio thread, of which
 * only the last before a frame could ever be seen or heard. This keeps the
 * latest value per key and sends it once per WINDOW_MS; `flush()` sends what
 * is waiting at once (the end of a gesture must not lose its last value).
 *
 * A timer, not the display's frame callback: a WKWebView that takes its window
 * for hidden services none, and a drag's values then left only on release
 * (the kit's rule; tests/editor_timing.test.mjs).
 */

/** About one display frame. */
export const WINDOW_MS = 16;

export function createCoalescer(send, {
  after = (f, ms) => globalThis.setTimeout(f, ms),
  cancel = (id) => globalThis.clearTimeout(id),
} = {}) {
  const pending = new Map();
  let id = 0;
  const flush = () => {
    if (id) cancel(id);
    id = 0;
    const out = [...pending];
    pending.clear();
    for (const [key, value] of out) send(key, value);
  };
  return {
    push(key, value) {
      pending.set(key, value);
      if (!id) id = after(() => { id = 0; flush(); }, WINDOW_MS);
    },
    flush,
    pending: () => pending.size,
  };
}
