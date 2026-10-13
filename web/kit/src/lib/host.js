// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editor's host, both directions. Shared by every editor in this repository.
 *
 * The components speak one small vocabulary -- a parameter by INDEX with a
 * NORMALISED 0..1 value, a gesture around a drag, and tagged messages each way
 * -- and never touch whatever is on the other end. That end is a TRANSPORT,
 * connected once at startup:
 *
 *   connect(transport)
 *     transport.attach(inbound)      inbound = { param, message, bytes }:
 *                                    what the transport calls when the host
 *                                    says something
 *     transport.setParam(i, v)       a normalised write
 *     transport.beginGesture(i) / endGesture(i)
 *     transport.sendMessage(tag, text)
 *
 * This file was iplug.js, when the editors lived in iPlug2's WebView and the
 * transport was its globals. The plugins' editors are native JUCE now (ADR
 * 0002); these editors run on the Move, in Schwung's web manager, and their
 * transport is ./schwung.js. The vocabulary did not change, which is why the
 * editors did not have to.
 *
 * VALUES ARE NORMALISED 0..1, always, in both directions. The transport owns
 * the real range and the display text; the UI owns neither.
 */

let transport = null;
const held = new Map();
const paramListeners = new Set();
const messageListeners = new Set();
/* tag -> Set of fns: the tags whose payload is BYTES rather than text. */
const byteListeners = new Map();

/* The one place a parameter value reaches the components, whether it came from
 * the host or from the control the user is holding. */
function notifyParam(paramIdx, value) {
  for (const fn of paramListeners) fn(paramIdx, value);
}

const inbound = {
  /* A value from the host. Not while a gesture holds it: the UI is the author
   * of the value until the gesture ends, and an echo of 0.40 arriving after the
   * UI already wrote 0.50 would move the knob backwards under the pointer. */
  param(paramIdx, value) {
    const i = paramIdx | 0;
    if (held.has(i)) return;
    notifyParam(i, value);
  },
  message(tag, text) {
    const t = tag | 0;
    for (const fn of messageListeners) fn(t, typeof text === 'string' ? text : '');
  },
  /* A BINARY tag: the array is only valid during the call. */
  bytes(tag, data) {
    const set = byteListeners.get(tag | 0);
    if (set) for (const fn of set) fn(data);
  },
};

/** Connect the editor to its host. One transport per page; a second replaces it. */
export function connect(t) {
  transport = t;
  t?.attach?.(inbound);
}

/** The transport, for a test that wants to see what was sent. */
export const connected = () => transport;

/**
 * Set a parameter. `value` is normalised 0..1.
 *
 * APPLIED LOCALLY AS WELL AS SENT. A host need not echo a write back to its
 * author, and a control that renders from the value it was last TOLD would
 * then never see its own writes: a toggle that computes its next state from a
 * value that never changed toggles on once and then "does nothing". A later
 * value from the host still overwrites it -- the host remains authoritative.
 */
export const setParam = (paramIdx, value) => {
  transport?.setParam?.(paramIdx | 0, value);
  notifyParam(paramIdx | 0, value);
};

/*
 * A DRAG IS ONE GESTURE, NOT A HUNDRED EDITS. Counted, not flagged, so two
 * overlapping gestures on one parameter (a two-axis handle and a knob) release
 * it only when both have ended.
 */
export const beginGesture = (paramIdx) => {
  const i = paramIdx | 0;
  held.set(i, (held.get(i) ?? 0) + 1);
  transport?.beginGesture?.(i);
};
export const endGesture = (paramIdx) => {
  const i = paramIdx | 0;
  const n = (held.get(i) ?? 0) - 1;
  if (n > 0) held.set(i, n); else held.delete(i);
  transport?.endGesture?.(i);
};

export const sendMessage = (msgTag, text = '') => transport?.sendMessage?.(msgTag | 0, text);

export const onParam = (fn) => { paramListeners.add(fn); return () => paramListeners.delete(fn); };
export const onMessage = (fn) => { messageListeners.add(fn); return () => messageListeners.delete(fn); };
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
