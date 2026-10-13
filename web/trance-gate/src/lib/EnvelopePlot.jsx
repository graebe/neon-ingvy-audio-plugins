// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The envelope plot under the ring: one gate on a millisecond axis.
 */
import { For, createMemo } from 'solid-js';
import { Well, Axis, INSET, CAPTION } from '@ultraviolet/ui';
import { levelAt } from './capture.js';
import { Curve, Rule } from './StepMarks.jsx';
import { INFO } from './info.js';

/*
 * THE AXIS, ON MILLISECONDS.
 *
 * Not "one step": the span is computed, and the envelope may run PAST the
 * step's edge -- which is the case the amber rule and the ghost curve exist
 * to explain. The native editor's EnvelopePlot::spanOf:
 *
 *   widthMs = hold * msStep              the gate's open time
 *   stageMs = pct * widthMs / 100        a stage is a % OF THE WIDTH
 *   spanMs  = max(width + r, a + d, msStep, 2) * 1.04
 *
 * The span holds BOTH curves: the gated one ends at width + release -- the
 * gate shuts at its width whatever stage is running, so the release starts
 * there -- and the ghost, the envelope as dialled, runs on to attack + decay.
 * The 4% of air puts the release inside the well rather than on its frame.
 */
export function spanOf(p) {
  if (!p || !(p.msStep > 0)) return null;
  const msStep = p.msStep;
  const hold = Math.min(1, Math.max(0, p.width));
  const widthMs = hold * msStep;
  const k = widthMs * 0.01;
  const attackMs = Math.max(0, p.attack) * k;
  const decayMs = Math.max(0, p.decay) * k;
  const releaseMs = Math.max(0, p.release) * k;
  return {
    msStep, hold, attackMs, decayMs, releaseMs, gateMs: widthMs,
    spanMs: Math.max(widthMs + releaseMs, attackMs + decayMs, msStep, 2) * 1.04,
    amount: Math.min(1, Math.max(0, p.amount ?? 1)),
    /* The release runs past the step's end -- the next gate will cut it. */
    truncated: widthMs + releaseMs > msStep + 1e-6,
    /* The gate closed before the decay finished, so the gated curve alone
     * would be a puzzle: you set a long decay and see a spike. */
    early: widthMs < attackMs + decayMs - 1e-6,
  };
}

export function EnvelopePlot(props) {
  const env = createMemo(() => spanOf(props.params));

  /*
   * THE CURVES ARE THE ENGINE'S, NOT A MODEL OF IT (tg_core_render_envelope,
   * in fractions of a step): `gated` one gate as it sounds, the ghost the
   * envelope as dialled with no gate over it. This only maps them onto the
   * millisecond axis.
   */
  const samples = (gated) => {
    const sh = env();
    const e = props.envelope;
    if (!sh || !e) return [];
    const values = gated ? e.gated : e.ghost;
    return Array.from({ length: 240 }, (_, i) =>
      levelAt(values, e.perStep, (sh.spanMs * (i / 239)) / sh.msStep));
  };

  const caption = () => {
    const sh = env();
    return sh ? `ENVELOPE ${Math.round(sh.spanMs)} MS   STEP ${sh.msStep.toFixed(1)} MS` : '';
  };

  /* Two strips along the bottom -- the millisecond axis under the stage
   * letters -- so neither ever sits on top of the curve. */
  const LABEL_H = 12, AXIS_H = 14;
  const boxBottom = () => props.h - INSET - LABEL_H - AXIS_H;
  const xAt = (f) => INSET + (props.w - 2 * INSET) * Math.min(1, Math.max(0, f));

  return (
    <Well w={props.w} h={props.h} info={INFO.envelopePlot} caption={caption()}>
      {env() && (() => {
        const sh = env();
        const top = CAPTION, bot = boxBottom();
        const floor = 1 - sh.amount;
        const aEnd = sh.attackMs, dEnd = sh.attackMs + sh.decayMs;
        const rEnd = sh.gateMs + sh.releaseMs;

        /*
         * THE LETTERS LABEL WHAT RAN, NOT WHAT WAS DIALLED. Each stage is
         * clamped to the gate, which shuts whatever stage is running, so a
         * stage the gate cut short collapses to nothing and is left out, as
         * is one narrower than its letter.
         */
        const clamp = (ms) => Math.min(ms, sh.gateMs);
        const segs = [['A', 0, clamp(aEnd)],
                      ['D', clamp(aEnd), clamp(dEnd)],
                      ['S', clamp(dEnd), sh.gateMs],
                      ['R', sh.gateMs, rEnd]];

        return (
          <>
            {/* Where the gate really closes, when it closes before the step's
              * end: a position, so a rule. */}
            {sh.hold < 1 && <Rule x={xAt(sh.gateMs / sh.spanMs)} top={top} bottom={bot} />}
            {/* THE GHOST FIRST, UNDERNEATH: the envelope as dialled, in the
              * plot-ghost hairline, behind what the gate leaves of it. */}
            {sh.early && (
              <Ghost values={samples(false)} w={props.w} top={top} bottom={bot} floor={floor} />
            )}
            <Curve values={samples(true)} w={props.w} top={top} bottom={bot} floor={floor} />

            <For each={segs}>{([ch, from, to]) => {
              const x0 = xAt(from / sh.spanMs), x1 = xAt(to / sh.spanMs);
              return (to > from && x1 - x0 >= 12) ? (
                <text class="t-hint plot-caption" x={(x0 + x1) / 2} y={bot + LABEL_H - 1}
                      text-anchor="middle">{ch}</text>
              ) : null;
            }}</For>

            {/* The step's edge, amber when the envelope runs past it. */}
            <Rule x={xAt(sh.msStep / sh.spanMs)} top={top} bottom={bot} warn={sh.truncated} />

            <Axis w={props.w} y={props.h - INSET - AXIS_H} spanMs={sh.spanMs} markMs={sh.msStep} />
          </>
        );
      })()}
    </Well>
  );
}

/* The envelope as dialled: a plot-ghost hairline over the same floor. */
function Ghost(props) {
  const d = () => {
    const v = props.values;
    if (!v.length) return '';
    const h = (props.bottom - props.top) * (1 - props.floor);
    return v.map((g, i) =>
      `${i ? 'L' : 'M'} ${(INSET + (props.w - 2 * INSET) * (i / (v.length - 1))).toFixed(2)} ${(props.top + h * (1 - Math.min(1, Math.max(0, g)))).toFixed(2)}`).join(' ');
  };
  return <path d={d()} fill="none" stroke="var(--plot-ghost)" stroke-width="1" />;
}
