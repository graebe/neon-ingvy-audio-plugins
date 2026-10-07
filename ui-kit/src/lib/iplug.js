// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The iPlug2 bridge, both directions. Shared by every editor in this repository.
 *
 * THIS IS A PROPERTY OF iPlug2, NOT OF ANY PLUGIN, which is why it lives in the
 * kit. It existed twice -- once per editor -- and the Spectrogram's copy said
 * so in a comment: "A COPY, AND KNOWINGLY SO ... the moment there is a third
 * plugin, this and uv.css move into one and both editors import them." There is
 * a workspace now, so they have.
 *
 * The copies had already drifted: only the Trance Gate's carried the local
 * apply in setParam below, so the Spectrogram's Select had the same latent bug
 * the Trance Gate's Join Neighbors was reported for. Sharing the file fixes it
 * rather than porting the fix.
 *
 * THE MESSAGE TAGS ARE NOT HERE. Each plugin's EMsgTags is its own -- the
 * Trance Gate's pattern and the Spectrogram's columns have nothing to say to
 * each other -- so each editor defines its own MSG table and passes tags in.
 *
 * iPlug2 injects one function into the page -- IPlugSendMsg -- and calls a
 * handful of globals on it. Everything below is those two facts wrapped so
 * the components never touch a global directly.
 *
 * VALUES ON THE WIRE ARE NORMALISED 0..1, always, in both directions. The
 * plugin owns the real range and the display text; the UI owns neither and
 * must not invent them, or a knob and its readout start disagreeing about
 * what the same parameter means.
 */

const send = (m) => {
  if (typeof globalThis.IPlugSendMsg === 'function') globalThis.IPlugSendMsg(m);
};

/**
 * Set a parameter. `value` is normalised 0..1.
 *
 * APPLIED LOCALLY AS WELL AS SENT, AND THAT IS NOT AN OPTIMISATION.
 *
 * iPlug2's inbound handler is, in full:
 *
 *     if (json["msg"] == "SPVFUI")
 *       SendParameterValueFromUI(json["paramIdx"], json["value"]);
 *
 * and that does SetNormalized() plus OnParamChangeUI(). NOTHING comes back to
 * the page. So a control that renders from the value it was last TOLD never
 * sees its own writes, and only moves if the host happens to echo the change
 * back -- which Live does for continuous parameters and does not reliably do
 * for a bool or an enum.
 *
 * That was two reported faults. Join Neighbors sent `1` on every click
 * forever, because it computed the next state from a value that never
 * changed, so it toggled on once and then "did nothing". The Curve select
 * moved its native element and left the label reading "Linear", because the
 * label is derived from the value too.
 *
 * The UI is the author of this change, so it may act on it. A later SPVFD
 * from the plugin still overwrites it -- the plugin remains authoritative,
 * including for the quantisation an int parameter applies -- and every
 * readout's TEXT already comes from the plugin regardless.
 */
export const setParam = (paramIdx, value) => {
  send({ msg: 'SPVFUI', paramIdx: paramIdx | 0, value });
  notifyParam(paramIdx | 0, value);
};

/*
 * A DRAG IS ONE GESTURE, NOT A HUNDRED EDITS. Begin/end bracket it so the
 * host writes a single undo step and an automation lane records a touch
 * rather than a hundred separate writes.
 *
 * AND WHILE IT IS OPEN THE HOST'S ECHOES ARE NOT APPLIED. The host echoes each
 * write back a little later; mid-drag an echo of 0.40 arriving after the UI
 * already wrote 0.50 moved the knob backwards under the pointer. The UI is the
 * author of the value until the gesture ends, and the host is again after it.
 * Counted, not flagged, so two overlapping gestures on one parameter (a
 * two-axis handle and a knob) release it only when both have ended.
 */
const held = new Map();

export const beginGesture = (paramIdx) => {
  const i = paramIdx | 0;
  held.set(i, (held.get(i) ?? 0) + 1);
  send({ msg: 'BPCFUI', paramIdx: i });
};
export const endGesture = (paramIdx) => {
  const i = paramIdx | 0;
  const n = (held.get(i) ?? 0) - 1;
  if (n > 0) held.set(i, n); else held.delete(i);
  send({ msg: 'EPCFUI', paramIdx: i });
};

/* SAMFUI carries its payload base64-encoded; the plugin decodes before it
 * ever sees the bytes, so anything not encoded here arrives as nonsense. */
const encoder = new TextEncoder();
const b64 = (str) => {
  const bytes = encoder.encode(str);
  let bin = '';
  for (const b of bytes) bin += String.fromCharCode(b);
  return btoa(bin);
};

export const sendMessage = (msgTag, text = '') =>
  send({ msg: 'SAMFUI', msgTag: msgTag | 0, ctrlTag: -1, data: b64(text) });

/*
 * Plugin -> UI. The plugin calls these by name on the page, so they are
 * assigned to globalThis rather than exported for import: nothing in the page
 * calls them, and if they are missing the plugin's notifications land nowhere
 * and the UI silently stops tracking the host.
 */
const paramListeners = new Set();
const messageListeners = new Set();
/* tag -> Set of fns: the tags whose payload is BYTES rather than text. */
const byteListeners = new Map();

export const onParam = (fn) => { paramListeners.add(fn); return () => paramListeners.delete(fn); };
export const onMessage = (fn) => { messageListeners.add(fn); return () => messageListeners.delete(fn); };
/**
 * A BINARY tag: its payload arrives as a Uint8Array and is never run through
 * the text decoder. The plot captures travel as raw bytes -- a byte per bound
 * -- and decoding them as UTF-8 would mangle every value past 0x7F.
 * The array is only valid during the call; copy what must outlive it.
 */
export const onBytes = (tag, fn) => {
  const t = tag | 0;
  if (!byteListeners.has(t)) byteListeners.set(t, new Set());
  byteListeners.get(t).add(fn);
  return () => {
    const set = byteListeners.get(t);
    set?.delete(fn);
    if (set && !set.size) byteListeners.delete(t);
  };
};

/* The one place a parameter value reaches the components, whether it came from
 * the plugin or from the control the user is holding. */
function notifyParam(paramIdx, value) {
  for (const fn of paramListeners) fn(paramIdx, value);
}

globalThis.SPVFD = (paramIdx, value) => {
  const i = paramIdx | 0;
  if (held.has(i)) return;           /* an echo of our own gesture */
  notifyParam(i, value);
};
/*
 * DECODE. SendArbitraryMsgFromDelegate BASE64-ENCODES on the way out, and
 * nothing says so at the call site -- the payload simply arrives as
 * "MTAwLjAwICU=" instead of "100.00 %".
 *
 * Handing that straight to the UI put base64 in every readout, and it was the
 * quieter half that mattered: the `ui`, `params` and `scope` messages are
 * encoded too, so every split(':') found no fields, every parse bailed, and
 * the ring, the pads and the plots drew nothing at all. One missing atob read
 * as five missing components.
 *
 * TextDecoder rather than String.fromCharCode over the bytes, because the
 * strings carry "%" and "µ" and a byte-wise read mangles anything past ASCII.
 */
/*
 * DECODED ONCE: base64 to bytes, and bytes to text only for a text tag, with
 * one decoder for the page's lifetime rather than one per message.
 */
const decoder = new TextDecoder();

/** base64 -> Uint8Array, or null when it is not base64. */
export const decodeBase64 = (msg) => {
  if (typeof msg !== 'string') return null;
  if (msg === '') return new Uint8Array(0);
  let bin;
  try { bin = atob(msg); } catch { return null; }
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return bytes;
};

globalThis.SAMFD = (msgTag, dataSize, msg) => {
  const tag = msgTag | 0;
  const bytes = decodeBase64(msg);
  const bin = byteListeners.get(tag);
  if (bin) {
    if (bytes) for (const fn of bin) fn(bytes);
    return;
  }
  /* Not base64 after all -- a future iPlug2 that stops encoding, or a
   * message sent from somewhere else. Pass it through rather than lose it. */
  const text = bytes ? decoder.decode(bytes) : (typeof msg === 'string' ? msg : '');
  for (const fn of messageListeners) fn(tag, text);
};
/* Declared even though nothing uses them yet: iPlug2 calls all four, and an
 * undefined global is a TypeError inside the WebView that no one sees. */
globalThis.SCVFD = () => {};
globalThis.SCMFD = () => {};
