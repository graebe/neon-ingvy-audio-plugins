/*
 * At most one message per key per frame. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * A pointer reports far more often than the display draws -- a pad dragged for
 * its amount sent a message on every pointermove, each one a base64 frame
 * across the WebView bridge and an edit queued for the audio thread, of which
 * only the last before a frame could ever be seen or heard. This keeps the
 * latest value per key and sends it once a frame; `flush()` sends what is
 * waiting at once (the end of a gesture must not lose its last value).
 */
export function createCoalescer(send, {
  raf = (f) => globalThis.requestAnimationFrame(f),
  caf = (id) => globalThis.cancelAnimationFrame(id),
} = {}) {
  const pending = new Map();
  let id = 0;
  const flush = () => {
    if (id) caf(id);
    id = 0;
    const out = [...pending];
    pending.clear();
    for (const [key, value] of out) send(key, value);
  };
  return {
    push(key, value) {
      pending.set(key, value);
      if (!id) id = raf(() => { id = 0; flush(); });
    },
    flush,
    pending: () => pending.size,
  };
}
