// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The host parameters, as one store every editor reads.
 *
 * It existed three times -- the Trance Gate's and Side-Chain's params.jsx and
 * Listen-In's inline version -- and each held the values as ONE array signal,
 * so every echo from the host re-evaluated every knob in the window. Here each
 * parameter has its own signals: a value, the plugin's display string and its
 * default.
 *
 * THE DEFAULT IS THE PLUGIN'S. The shell sends every parameter's normalised
 * default (SHELL_MSG.defaults) with the state, and a reset sets exactly that.
 * Before it the Trance Gate reset every knob but Fade to 0 and Side-Chain took
 * the first value it happened to see, which is the SESSION's value -- so a
 * reset of a reopened editor reset to wherever it had been left.
 *
 * Plain JavaScript, so `node --test` can reach it; the controls that bind to it
 * are components/Param.jsx.
 */
import { createSignal } from 'solid-js';
import {
  onParam, onMessage, setParam, beginGesture, endGesture, sendMessage,
} from './iplug.js';
import { SHELL_MSG } from './shell.js';

/**
 * "<d0>:<d1>:..." -> one entry per parameter: a number in 0..1, or undefined
 * for a field that is not one. Nothing is invented for a missing field -- a
 * reset of a parameter whose default is unknown does nothing rather than
 * something plausible.
 */
export function parseDefaults(text, count) {
  const out = new Array(count).fill(undefined);
  if (typeof text !== 'string' || text === '') return out;
  const f = text.split(':');
  for (let i = 0; i < count && i < f.length; i++) {
    const v = f[i].trim() === '' ? NaN : Number(f[i]);
    if (Number.isFinite(v) && v >= 0 && v <= 1) out[i] = v;
  }
  return out;
}

/**
 * A store for `count` host parameters, listening from the moment it is made.
 * Call it before the editor sends SHELL_MSG.ready, which every editor does
 * through useEditorBridge.
 */
export function createParams(count) {
  const n = Math.max(0, count | 0);
  const make = (init) => Array.from({ length: n }, () => createSignal(init));
  const values = make(0);
  const texts = make('');
  const defaults = make(undefined);

  const inRange = (i) => i >= 0 && i < n;

  const offParam = onParam((i, v) => { if (inRange(i)) values[i][1](v); });
  const offText = onMessage((tag, text) => {
    if (inRange(tag)) texts[tag][1](text);
    else if (tag === SHELL_MSG.defaults) {
      parseDefaults(text, n).forEach((d, i) => defaults[i][1](d));
    }
  });

  const commit = (i, v) => {
    if (!inRange(i)) return;
    beginGesture(i);
    setParam(i, v);
    endGesture(i);
  };

  return {
    count: n,
    /** Normalised 0..1, as the host last said or the UI last wrote. */
    value: (i) => (inRange(i) ? values[i][0]() : 0),
    /** The plugin's display string. The UI formats nothing. */
    text: (i) => (inRange(i) ? texts[i][0]() : ''),
    /** The plugin's normalised default, or undefined until it has said. */
    defaultOf: (i) => (inRange(i) ? defaults[i][0]() : undefined),

    /* A drag: one gesture around many writes. */
    begin: (i) => inRange(i) && beginGesture(i),
    input: (i, v) => inRange(i) && setParam(i, v),
    end: (i) => inRange(i) && endGesture(i),
    /** One write, one undo step. */
    commit,
    /** Back to the plugin's default; nothing if it has not been told one. */
    reset: (i) => {
      const d = inRange(i) ? defaults[i][0]() : undefined;
      if (d !== undefined) commit(i, d);
    },
    /** Typed text, parsed by the parameter itself in the plugin. */
    sendText: (i, text) => inRange(i) && sendMessage(SHELL_MSG.setText, `${i}:${text}`),

    dispose: () => { offParam(); offText(); },
  };
}
