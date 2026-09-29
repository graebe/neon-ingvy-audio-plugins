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
  setText: 100, rows: 101,
  /*
   * "I AM LISTENING", and it has to exist because the plugin's push on open
   * CANNOT be heard.
   *
   * OnUIOpen fires from didFinishNavigation and calls SPVFD() twelve times.
   * But this editor is a <script type="module">, module scripts are DEFERRED,
   * and so they evaluate AFTER the document is done -- globalThis.SPVFD does
   * not exist yet and all twelve values are dropped. The UI then sat on
   * twelve zeroes until something was touched, which showed up as four
   * separate faults: a knob whose first drag jumped to zero, a switch drawn
   * off whatever the engine held, and two dropdowns stuck on their first
   * entry.
   *
   * Sent from onMount, so it cannot be early.
   */
  ready: 102,
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

/* The one place a parameter value reaches the components, whether it came from
 * the plugin or from the control the user is holding. */
function notifyParam(paramIdx, value) {
  for (const fn of paramListeners) fn(paramIdx, value);
}

globalThis.SPVFD = (paramIdx, value) => notifyParam(paramIdx | 0, value);
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
const decode = (msg) => {
  if (typeof msg !== 'string' || msg === '') return '';
  try {
    const bin = atob(msg);
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    return new TextDecoder().decode(bytes);
  } catch {
    /* Not base64 after all -- a future iPlug2 that stops encoding, or a
     * message sent from somewhere else. Pass it through rather than lose it. */
    return msg;
  }
};

globalThis.SAMFD = (msgTag, dataSize, msg) => {
  const text = decode(msg);
  for (const fn of messageListeners) fn(msgTag | 0, text);
};
/* Declared even though nothing uses them yet: iPlug2 calls all four, and an
 * undefined global is a TypeError inside the WebView that no one sees. */
globalThis.SCVFD = () => {};
globalThis.SCMFD = () => {};
