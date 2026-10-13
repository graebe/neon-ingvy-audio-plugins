// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The transport to a Move: Schwung's web manager, through the bridge it serves
 * every module page (/static/schwung-remote-api.js, `window.schwungRemote`).
 * Connected with lib/host.js's connect(); docs/tech/move-web-editors.md has
 * the whole picture.
 *
 *   createSchwungTransport({
 *     bridge,                   window.schwungRemote, or a fake one
 *     params: ['source', ...],  the editor's parameter INDEX -> the module's
 *                               chain_params key; null where the Move has no
 *                               such parameter
 *     feeds: { ui: 64, ... },   the module's live readouts (its `web_ui`
 *                               declaration's extra_keys) -> the message tag
 *                               the editor already listens on
 *     messages: { [tag]: (text, write) => void },
 *                               the editor's own messages, as writes:
 *                               write(key, value)
 *     defaultsTag, setTextTag,  the shell's tags (lib/shell.js)
 *   })
 *
 * WHAT ARRIVES, AND HOW. The browser cannot ask the Move for a value; the
 * manager pushes them. Every key comes as `<component>:<key>` -- the component
 * is the page's position, `fx1`, `master_fx:fx2` -- and a value is a string:
 *
 *   - a declared parameter: once when the page subscribes, then whenever
 *     anybody writes it (the jog, a knob, another browser, this page);
 *   - a feed: whenever any value of the component changed (150 ms at most)
 *     and twice a second besides, when it moved.
 *
 * WHAT GOES OUT. A write is `setParam('<component>:<key>', wire)`: under 256
 * bytes, in order, lossless. A float-looking value goes through the chain's
 * smoother and its automation recorder, which is what a knob's write should do.
 *
 * Nothing is normalised until chain_params has arrived: the range is the
 * module's to declare. What came earlier is kept and applied then.
 */
import { paramOf } from './chain.js';

/* chain_params can come back null while the manager has not read it yet (it
 * builds the page on the module's arrival, not on the declaration's). */
const LEARN_TRIES = 20;
const LEARN_WAIT_MS = 250;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export function createSchwungTransport({
  bridge, params = [], feeds = {}, messages = {}, defaultsTag, setTextTag, wait = sleep,
}) {
  const prefix = `${bridge.component || 'synth'}:`;
  const indexOf = new Map(params.map((k, i) => [k, i]).filter(([k]) => k));
  let meta = null;           /* index -> paramOf(entry) */
  const early = new Map();   /* key -> wire, before meta */
  let inbound = null;

  const write = (key, value) => bridge.setParam(prefix + key, String(value));

  function applyParam(key, wire) {
    const i = indexOf.get(key);
    const p = meta?.[i];
    if (!p) return;
    const n = p.norm(wire);
    if (n === undefined) return;
    inbound.param(i, n);
    inbound.message(i, p.text(wire));
  }

  function receive(changes) {
    for (const [full, raw] of Object.entries(changes ?? {})) {
      if (!full.startsWith(prefix)) continue;
      const key = full.slice(prefix.length);
      const value = String(raw ?? '');
      if (Object.prototype.hasOwnProperty.call(feeds, key)) {
        inbound.message(feeds[key], value);
      } else if (indexOf.has(key)) {
        if (meta) applyParam(key, value);
        else early.set(key, value);
      }
    }
  }

  async function learn() {
    let declared = null;
    for (let i = 0; i < LEARN_TRIES && !(declared?.length); i++) {
      if (i) await wait(LEARN_WAIT_MS);
      declared = await bridge.getChainParams?.();
    }
    if (!declared?.length) return false;
    const byKey = new Map(declared.filter((e) => e && e.key).map((e) => [e.key, e]));
    meta = params.map((k) => (k ? paramOf(byKey.get(k)) : null));
    if (defaultsTag !== undefined) {
      inbound.message(defaultsTag, meta.map((p) => (p?.defaultNorm ?? '')).join(':'));
    }
    for (const [k, v] of early) applyParam(k, v);
    early.clear();
    return true;
  }

  return {
    /* Subscribing makes the manager replay every value it holds for the
     * slot, so a page that opens late still starts complete. Resolves to
     * whether chain_params arrived. */
    attach(i) {
      inbound = i;
      bridge.onParamChange(receive);
      return learn();
    },
    setParam(i, n) {
      const p = meta?.[i];
      if (p) write(p.key, p.wire(n));
    },
    /* Schwung records no gestures: a drag is a stream of writes, which the
     * chain's smoother already treats as one movement. */
    beginGesture() {},
    endGesture() {},
    sendMessage(tag, text = '') {
      if (tag === setTextTag) {
        const at = text.indexOf(':');
        const p = meta?.[Number(text.slice(0, at))];
        const wire = p?.parse(text.slice(at + 1));
        if (wire !== undefined) write(p.key, wire);
        return;
      }
      messages[tag]?.(text, write);
    },
  };
}
