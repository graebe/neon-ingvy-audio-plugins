/*
 * This editor's message tags. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The shell's tags are the kit's (@ultraviolet/ui/shell), the same in every
 * plugin; the rest are this plugin's own vocabulary. tests/editor_tags.test.mjs
 * holds both to the C++.
 */
import { SHELL_MSG } from '@ultraviolet/ui/shell';

/* EMsgTags in SideChain.h, after the shell's. Tags 0..14 are a parameter's own
 * display string, tagged with its index -- which is why this editor knows no
 * unit, precision or enum label. */
export const MSG = {
  ...SHELL_MSG,
  /* plugin -> UI */
  uiState: 64,
  params: 65,
  scope: 66,
  stageMs: 67,
  buses: 68,
};

/* The parameter indices, in SideChain.h's EParams order -- which is the engine's
 * sc_param_t order, so a host index is an engine index. */
export const P = {
  source: 0,
  rate: 1,
  timeMode: 2,
  delay: 3,
  attack: 4,
  hold: 5,
  release: 6,
  depth: 7,
  curve: 8,
  channel: 9,
  note: 10,
  midiMode: 11,
  velSens: 12,
  threshold: 13,
  lockout: 14,
};
export const NUM_PARAMS = 15;

/* The enum option counts, so a normalised value can be turned back into an
 * index. The LABELS come from the plugin as display strings; only the counts
 * are needed here, and they are what a Select needs to do its arithmetic. */
export const COUNTS = {
  [P.source]: 3,
  [P.rate]: 12,
  [P.timeMode]: 2,
  [P.curve]: 3,
  [P.channel]: 17,
  [P.note]: 128,
  [P.midiMode]: 2,
};

/*
 * THE PARAMETER RANGES, MIRRORED FROM Params.cpp's InitDouble CALLS.
 *
 * A duplicated range is exactly the kind of thing that drifts silently -- the
 * handle would land in the wrong place and look like a drawing bug -- so
 * `test/ranges.test.mjs` parses Params.cpp and fails if these disagree.
 *
 * They are needed because a host parameter is a 0..1 number and the drawing is
 * in percent of the cycle. The `params` readout carries the engine's own values
 * and is what the SHAPE is drawn from; these are for the other direction, where
 * a pointer position has to become a normalised value to send back.
 */
export const RANGES = {
  /* BOTH WAYS. Negative is an early sidechain; see Params.cpp, which this
   * mirrors and which test/ranges.test.mjs parses to keep the two in step. */
  [P.delay]: [-100, 100],
  [P.attack]: [0, 200],
  [P.hold]: [0, 200],
  [P.release]: [0, 200],
  [P.depth]: [0, 100],
  [P.velSens]: [0, 100],
  [P.threshold]: [-60, 0],
  [P.lockout]: [0, 200],
};

/** A value in a parameter's own unit -> the 0..1 a host wants. */
export const toNorm = (idx, v) => {
  const [lo, hi] = RANGES[idx] ?? [0, 1];
  if (hi === lo) return 0;
  return Math.min(1, Math.max(0, (v - lo) / (hi - lo)));
};

/** And back. */
export const fromNorm = (idx, n) => {
  const [lo, hi] = RANGES[idx] ?? [0, 1];
  return lo + (hi - lo) * Math.min(1, Math.max(0, n));
};
