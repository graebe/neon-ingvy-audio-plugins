// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A Move, for a page that has none: Schwung's web manager and a module's DSP,
 * in the browser. The editors' development harness and their end-to-end tests
 * run on it; on a Move the page gets the real bridge instead.
 *
 *   createFakeSchwung({
 *     chainParams,      the module's declaration, as the device serves it
 *     engine,           { set(key, value), get(key) -> string | null,
 *                         advance(ms) } -- the real engine, as wasm, behind
 *                       the module's own get/set (see web/<p>/harness)
 *     component,        'fx1' by default
 *     timers,           { setInterval, clearInterval, setTimeout } -- a
 *                       test's own clock
 *     frameMs,          how often the engine is advanced, 20 by default
 *     latency,          () -> ms: how late each push reaches the page, or
 *                       null for at once. The real one is 20 to 400 ms and
 *                       different every time (the manager, the shim, WiFi,
 *                       the iframe) -- which is what a playhead has to survive
 *   }) -> { bridge, device(key, value), stop() }
 *
 * `bridge` is what /static/schwung-remote-api.js gives a page in an iframe
 * (window.schwungRemote). `device(key, value)` is a write from the Move's side:
 * a knob, the jog, a slot recall.
 *
 * IT DOES WHAT THE MANAGER DOES (schwung-manager/remote_ui.go), not more:
 *
 *   - subscribing pushes the FEEDS first -- the extra_keys the declaration
 *     names that are not params of their own -- then every declared value;
 *   - a write, from the page or the device, is echoed as the key that was
 *     written, truncated to 255 bytes as the notify ring truncates it;
 *   - after a write the feeds are re-read, at most every 150 ms;
 *   - twice a second the feeds are re-read anyway, and only what moved is sent.
 *
 * So a page that works against this works against the Move's timing: a value
 * the engine changes by itself (the playhead) arrives only through a feed,
 * never on its own.
 */

const ECHO_MAX = 255;
const AFTER_WRITE_MS = 150;
const HEARTBEAT_MS = 500;

/* The extra_keys of every declaration, minus the declared keys: what the
 * manager calls extraKeysOf. */
export function feedsOf(chainParams) {
  const declared = new Set(chainParams.map((p) => p && p.key).filter(Boolean));
  const out = [];
  for (const p of chainParams) {
    for (const list of [p?.extra_keys, p?.viz?.extra_keys]) {
      for (const k of list ?? []) {
        if (!declared.has(k) && !out.includes(k)) out.push(k);
      }
    }
  }
  return out;
}

export function createFakeSchwung({
  chainParams, engine, component = 'fx1', timers = globalThis, frameMs = 20, latency = null,
}) {
  const prefix = `${component}:`;
  const feeds = feedsOf(chainParams);
  const declared = chainParams
    .filter((p) => p && p.key && p.type !== 'canvas')
    .map((p) => p.key);
  const listeners = new Set();
  const lastFeed = new Map();
  let lastFeedAt = -Infinity;
  let now = 0;
  let subscribed = false;
  let pendingFeeds = false;

  const deliver = (params) => { for (const fn of listeners) fn(params); };
  const emit = (params) => {
    if (!Object.keys(params).length) return;
    if (latency) timers.setTimeout(() => deliver(params), latency());
    else deliver(params);
  };

  function pushFeeds(all = false) {
    const out = {};
    for (const k of feeds) {
      const v = engine.get(k);
      if (v === null || v === undefined) continue;
      if (!all && lastFeed.get(k) === v) continue;
      lastFeed.set(k, v);
      out[prefix + k] = v;
    }
    lastFeedAt = now;
    pendingFeeds = false;
    emit(out);
  }

  function write(key, value) {
    engine.set(key, String(value));
    if (!subscribed) return;
    emit({ [prefix + key]: String(value).slice(0, ECHO_MAX) });
    pendingFeeds = true;
  }

  /* One clock for the engine, the throttled re-read and the heartbeat. */
  let sinceBeat = 0;
  const frame = timers.setInterval(() => {
    now += frameMs;
    engine.advance(frameMs);
    if (!subscribed) return;
    sinceBeat += frameMs;
    if (pendingFeeds && now - lastFeedAt >= AFTER_WRITE_MS) pushFeeds();
    else if (sinceBeat >= HEARTBEAT_MS) {
      sinceBeat = 0;
      pushFeeds();
    }
  }, frameMs);

  const bridge = {
    component,
    onParamChange(fn) {
      listeners.add(fn);
      subscribed = true;
      pushFeeds(true);
      const values = {};
      for (const k of declared) {
        const v = engine.get(k);
        if (v !== null && v !== undefined) values[prefix + k] = v;
      }
      emit(values);
    },
    setParam(key, value) {
      if (String(key).startsWith(prefix)) write(String(key).slice(prefix.length), value);
    },
    getParam: async (key) => (String(key).startsWith(prefix) ? engine.get(String(key).slice(prefix.length)) : null),
    getChainParams: async () => chainParams,
    getHierarchy: async () => null,
  };

  return {
    bridge,
    device: (key, value) => write(key, value),
    stop: () => timers.clearInterval(frame),
  };
}
