/*
 * The iPlug2 bridge, both directions.
 * Copyright (c) 2026 Torben Gräber. MIT.
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

/** Set a parameter. `value` is normalised 0..1. */
export const setParam = (paramIdx, value) =>
  send({ msg: 'SPVFUI', paramIdx: paramIdx | 0, value });

/*
 * A DRAG IS ONE GESTURE, NOT A HUNDRED EDITS. Begin/end bracket it so the
 * host writes a single undo step and an automation lane records a touch
 * rather than a hundred separate writes.
 */
export const beginGesture = (paramIdx) =>
  send({ msg: 'BPCFUI', paramIdx: paramIdx | 0 });
export const endGesture = (paramIdx) =>
  send({ msg: 'EPCFUI', paramIdx: paramIdx | 0 });

/*
 * THE MESSAGE TAGS, mirroring EMsgTags in TranceGate.h. Tags 0..11 are a
 * parameter's display string, tagged with the parameter's own index.
 */
export const MSG = {
  uiState: 64, params: 65, scope: 66, patch: 67,
  setStep: 96, setDepth: 97, setCursor: 98, requestPatch: 99,
};

/* SAMFUI carries its payload base64-encoded; the plugin decodes before it
 * ever sees the bytes, so anything not encoded here arrives as nonsense. */
const b64 = (str) => {
  const bytes = new TextEncoder().encode(str);
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

export const onParam = (fn) => { paramListeners.add(fn); return () => paramListeners.delete(fn); };
export const onMessage = (fn) => { messageListeners.add(fn); return () => messageListeners.delete(fn); };

globalThis.SPVFD = (paramIdx, value) => {
  for (const fn of paramListeners) fn(paramIdx | 0, value);
};
globalThis.SAMFD = (msgTag, dataSize, msg) => {
  for (const fn of messageListeners) fn(msgTag | 0, msg);
};
/* Declared even though nothing uses them yet: iPlug2 calls all four, and an
 * undefined global is a TypeError inside the WebView that no one sees. */
globalThis.SCVFD = () => {};
globalThis.SCMFD = () => {};
