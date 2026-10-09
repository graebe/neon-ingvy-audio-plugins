// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the Side-Chain window SAYS about what it sees: the header's state and
 * its one amber warning, the plot's caption, the stages' readouts and the
 * playhead -- each a function of the feeds, kept apart from the components so
 * a test can read every case without drawing one.
 *
 * A PORT OF THE NATIVE EDITOR'S Status.cpp (plugins/side-chain/editor), case
 * for case, less what the Move cannot see. The plugin's first two warnings
 * were "no key routed" and "no input": the Move has no key input, and the
 * page has no audio to judge silence by -- "no input" was read off the scope,
 * and the Move streams no scope. What is left reads the `ui` feed alone.
 *
 * Plain JavaScript, so `node --test` can reach it.
 */
import { STAGES } from './text.js';

/* The sources, as the engine numbers them. */
export const CYCLE = 0;
export const MIDI = 1;

/*
 * WHETHER THE TRIGGER HAS GONE QUIET: no new fire for most of a second, so the
 * warnings that depend on it do not flicker between hits. Fed the engine's
 * fire counter with the time it was seen; a late look makes the verdict late,
 * never wrong the other way -- a fire is a change of the counter, and a
 * change is never missed.
 */
export const QUIET_AFTER_MS = 800;

export class TriggerWatch {
  constructor() {
    this.seen = false;
    this.lastFires = 0;
    this.lastChangeMs = 0;
  }

  observe(fires, nowMs) {
    if (!this.seen || fires !== this.lastFires) {
      this.seen = true;
      this.lastFires = fires;
      this.lastChangeMs = nowMs;
    }
  }

  isQuiet(nowMs) {
    return this.seen && nowMs - this.lastChangeMs > QUIET_AFTER_MS;
  }
}

/*
 * THE WINDOW'S ONE WARNING, lower case as the manual quotes it (the header
 * sets it in capitals), or ''. A ducker whose trigger is not arriving looks
 * exactly like one set to zero depth; on the Move that is a stopped transport
 * on Cycle, or no note on MIDI.
 */
export function warningFor(ui, triggerQuiet) {
  if (!triggerQuiet) return '';
  if (ui.source === CYCLE) return ui.advancing ? '' : 'transport stopped';
  if (ui.source === MIDI) return 'no midi';
  return '';
}

/* The header's state: the source, its rate on Cycle, and the stage --
 * "Cycle 1/4 · release". The names are the declaration's options. */
export function stateLine(sourceName, rateName, source, stage) {
  const rate = source === CYCLE && rateName ? ` ${rateName}` : '';
  return `${sourceName ?? ''}${rate} · ${STAGES[stage] ?? ''}`;
}

/*
 * THE PLOT'S CAPTION, in the PlotWell card's clauses three spaces apart: the
 * picture and its span ("SHAPE   ONE CYCLE, 500 MS"), then the stages' total
 * while they read in ms ("STAGES 225 MS"). The native caption ends with what
 * the grey behind is; there is no grey here.
 */
export function captionFor(msPerCycle, stagesMs = null) {
  let caption = 'SHAPE   ONE CYCLE';
  if (msPerCycle > 0) caption += `, ${Math.round(msPerCycle)} MS`;
  if (stagesMs !== null && stagesMs !== undefined) caption += `   STAGES ${Math.round(stagesMs)} MS`;
  return caption;
}

/* Whether the stages read in ms: Time on its first option, and a tempo known
 * -- before one the cycle has no length to read them in. */
export const readsInMs = (timeModeNorm, msPerCycle) => (timeModeNorm ?? 0) < 0.5 && msPerCycle > 0;

/*
 * A STAGE'S READOUT IN THE UNIT TIME ASKS FOR. The stages are percentages of
 * the cycle, always; Time only chooses how they read. In ms it is the engine's
 * own stage length ("40 ms"); in %, or before a tempo, the parameter's text.
 */
export const stageText = (stageMs, inMs, percentText) =>
  (inMs ? `${Math.round(stageMs)} ms` : percentText);

/*
 * AND TYPING INTO ONE: "40 ms" is milliseconds and "20 %" a percentage,
 * whatever the readout shows -- the unit you type wins -- and a bare number is
 * in the unit shown. Milliseconds become the percentage of the cycle they are
 * (the inverse of the engine's stage_ms), returned as { percent }; anything
 * else is the parameter's to read, returned as { text } for the transport to
 * parse against the declaration. null for text with no number, or for ms
 * before the cycle has a length.
 */
export function stageEdit(typed, inMs, msPerCycle) {
  const text = String(typed ?? '').trim();
  if (!/\d/.test(text)) return null;
  const saysMs = /ms$/i.test(text);
  const saysPercent = text.includes('%');
  if (saysMs || (inMs && !saysPercent)) {
    if (!(msPerCycle > 0)) return null;
    const ms = Number.parseFloat(text.replace(',', '.'));
    if (!Number.isFinite(ms)) return null;
    return { percent: (ms / msPerCycle) * 100 };
  }
  return { text };
}

/* The playhead, 0..1: the sweep, wrapping on Cycle, stopping at the end of a
 * one-shot duck elsewhere. */
export function playheadOf(sweep, source) {
  const s = Math.max(0, sweep || 0);
  if (s < 1) return s;
  return source === CYCLE ? s % 1 : 1;
}

/* The ruler's landmark: the instant the duck reaches its floor, in ms, or 0
 * when that is not inside the cycle. `bottom` is a percentage of the cycle. */
export const landmarkMs = (bottom, msPerCycle) =>
  (bottom > 0 && bottom <= 100 ? (bottom / 100) * msPerCycle : 0);

/*
 * A KEY ON A CHOICE KNOB MOVES AT LEAST ONE OPTION -- the native
 * ParamChoiceKnob::stepFrom. The kit's Knob steps by shares of its range (1 %
 * an arrow), which over 128 notes is about a semitone but over 17 channels is
 * a sixth of one, so an arrow would round back to where it was and do
 * nothing. A share is turned into whole options in its direction, never
 * fewer than one; the ends of the range stay the ends.
 *
 * Normalised in, normalised out, on `options` choices.
 */
export function stepFrom(from, to, options) {
  const last = options - 1;
  const clamp01 = (n) => Math.min(1, Math.max(0, n));
  if (last <= 0) return clamp01(to);
  if (to <= 0 || to >= 1) return to <= 0 ? 0 : 1;
  const at = Math.round(clamp01(from) * last);
  const share = (to - from) * last;
  if (Math.abs(share) < 1e-6) return at / last;
  const move = Math.max(1, Math.round(Math.abs(share)));
  const index = Math.min(last, Math.max(0, at + (share > 0 ? move : -move)));
  return index / last;
}
