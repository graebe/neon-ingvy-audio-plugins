// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The envelope plot under the ring: one gate on a millisecond axis.
 */
import { For, createMemo } from 'solid-js';
import { Well, Axis, INSET, CAPTION } from '@ultraviolet/ui';
import { levelAt } from './capture.js';
import { Curve } from './StepMarks.jsx';
import { INFO } from './info.js';

/*
 * THE ENVELOPE, ON A MILLISECOND AXIS.
 *
 * Not "one step": the span is computed, and the envelope may run PAST the
 * step's edge -- which is the case the amber rule and the ghost curve exist
 * to explain. The arithmetic is TgEnvelopeShape::render's, and all of it is
 * derivable here: the scratch engine the JUCE editor ran was only ever needed
 * for the curve's samples, and env_shape is ported above.
 *
 *   widthMs = hold * msStep              the gate's open time
 *   stageMs = pct * widthMs / 100        a stage is a % OF THE WIDTH
 *   gateMs  = widthMs                    where the gate closes, always
 *   spanMs  = max(gate + r, a + d, msStep, 2) * 1.04
 *
 * The span holds BOTH curves: the gated one ends at gate + release, the ghost
 * runs on to attack + decay. It used to be `max(gate, a+d) + r`, which waits
 * for a release the real envelope never waits for.
 *
 * The 4% of air at the end guarantees the release always lands inside the
 * well rather than on its frame.
 */
export function EnvelopePlot(props) {
  const env = createMemo(() => {
    const p = props.params;
    if (!p || !(p.msStep > 0)) return null;
    const msStep = p.msStep;
    const hold = Math.min(1, Math.max(0, p.width));
    const widthMs = hold * msStep;
    const k = widthMs * 0.01;
    const attackMs = Math.max(0, p.attack) * k;
    const decayMs = Math.max(0, p.decay) * k;
    const releaseMs = Math.max(0, p.release) * k;
    const gateMs = widthMs;
    /*
     * THE AXIS HAS TO HOLD BOTH CURVES, and they end at different places.
     *
     * The gated one ends at gate + release -- the gate shuts at `gate`
     * whatever stage is running, so the release starts THERE and not after
     * attack+decay. The ghost, which is the envelope as dialled, runs on to
     * attack + decay.
     *
     * This was `max(gate, attack+decay) + release`, which is neither: it
     * stretched the axis by a release the real envelope never waits for, so
     * with a long attack against a narrow Width the whole picture shrank into
     * the left of a well that was mostly empty.
     */
    const spanMs = Math.max(gateMs + releaseMs, attackMs + decayMs,
                            msStep, 2) * 1.04;
    return {
      msStep, hold, attackMs, decayMs, releaseMs, gateMs, spanMs,
      sustain: Math.min(1, Math.max(0, p.sustain)), curve: p.curve,
      amount: Math.min(1, Math.max(0, p.amount ?? 1)),
      /* The release ran past the step's end -- the next gate will cut it. */
      truncated: gateMs + releaseMs > msStep + 1e-6,
      /* The gate closed before the decay finished, so the solid curve alone
       * would be a puzzle: you set a long decay and see a spike. */
      early: gateMs < attackMs + decayMs - 1e-6,
    };
  });

  /*
   * THE CURVES ARE THE ENGINE'S, NOT A MODEL OF IT.
   *
   * This used to run the stage machine in JavaScript (curves.js), pinned to a
   * table of the engine's output -- a second implementation of DSP with an
   * oracle behind it. The plugin now renders both curves through a scratch
   * engine (tg_core_render_envelope) in fractions of a step, and this only maps
   * them onto the millisecond axis: `gated` is one gate as it sounds, the
   * ghost (`gated` false) the envelope as dialled with no gate over it.
   */
  const levelOf = (sh, ms, gated = true) => {
    const e = props.envelope;
    if (!e || !(sh.msStep > 0)) return 0;
    return levelAt(gated ? e.gated : e.ghost, e.perStep, ms / sh.msStep);
  };

  const samples = (gated) => {
    const sh = env();
    if (!sh || !props.envelope) return [];
    return Array.from({ length: 240 }, (_, i) => levelOf(sh, sh.spanMs * (i / 239), gated));
  };

  const caption = () => {
    const sh = env();
    return sh ? `ENVELOPE ${Math.round(sh.spanMs)} MS   STEP ${sh.msStep.toFixed(1)} MS` : '';
  };

  /* Two strips along the bottom -- the millisecond axis under the phase
   * letters -- so neither ever sits on top of the curve. */
  const LABEL_H = 12, AXIS_H = 14;
  const boxBottom = () => props.h - INSET - LABEL_H - AXIS_H;
  const xAt = (f) => INSET + (props.w - 2 * INSET) * Math.min(1, Math.max(0, f));

  return (
    <Well w={props.w} h={props.h} info={INFO.envelopePlot} caption={caption()}>
      {env() && (() => {
        const sh = env();
        const top = CAPTION, bot = boxBottom();
        /* One mapping for the curve, the ghost and the dots alike: they must
         * agree, or the markers float off the line they mark. */
        const yAt = (v) => top + hFloor * (1 - Math.min(1, Math.max(0, v)));

        const floor = 1 - Math.min(1, Math.max(0, sh.amount ?? 1));
        const hFloor = (bot - top) * (1 - floor);
        const aEnd = sh.attackMs, dEnd = sh.attackMs + sh.decayMs;
        const rEnd = sh.gateMs + sh.releaseMs;
        /* A stage the gate never reached did not happen, so it gets no
         * marker -- and two markers landing on the same millisecond are one
         * mark, not two drawn on top of each other. */
        const dots = [...new Set(
          [0, aEnd <= sh.gateMs ? aEnd : null,
           sh.decayMs > 0 && dEnd <= sh.gateMs ? dEnd : null,
           sh.gateMs,
           sh.releaseMs > 0 && rEnd <= sh.spanMs ? rEnd : null]
          .filter((m) => m !== null))];

        /*
         * THE LETTERS LABEL WHAT RAN, NOT WHAT WAS DIALLED.
         *
         * Each stage is clamped to the gate, because the gate shuts whatever
         * stage is running. Unclamped, a long attack against a narrow Width
         * printed "D" across the stretch AFTER the release had already taken
         * the envelope to zero -- a decay that never happened, labelled over a
         * curve that was doing the opposite. And "S" came out as a negative
         * span, which is the sustain fault as it actually appeared.
         *
         * Clamped, a stage the gate cut short collapses to zero width and the
         * skip below drops it, so the letters left are the stages you can hear.
         */
        const clamp = (ms) => Math.min(ms, sh.gateMs);
        const segs = [['A', 0, clamp(aEnd)],
                      ['D', clamp(aEnd), clamp(dEnd)],
                      ['S', clamp(dEnd), sh.gateMs],
                      ['R', sh.gateMs, rEnd]];
        const g = samples(true), gh = samples(false);
        const ghPath = () => gh.map((v, i) =>
          `${i ? 'L' : 'M'} ${(INSET + (props.w - 2 * INSET) * (i / (gh.length - 1))).toFixed(2)} ${(top + hFloor * (1 - Math.min(1, Math.max(0, v)))).toFixed(2)}`).join(' ');

        return (
          <>
            {/* Where the gate really closes: a rail-coloured hairline, since
              * it marks a position rather than a value. */}
            {sh.hold < 1 && (
              <line x1={xAt(sh.gateMs / sh.spanMs)} x2={xAt(sh.gateMs / sh.spanMs)}
                    y1={top} y2={bot} stroke="var(--line-200)" />
            )}
            {/* THE GHOST FIRST, UNDERNEATH: the envelope as dialled. The dim
              * line is the decay you asked for; the solid one is what the
              * gate leaves of it. */}
            {sh.early && (
              <path d={ghPath()} fill="none" stroke="var(--ink-dim)" stroke-width="1" />
            )}
            {/* ONE-SIDED, with the Amount floor: the gate never closes past
              * Amount, so everything below 1 - amount is lit whatever it
              * does. An envelope drawn as though it reached zero would be
              * lying about what you hear. */}
            <Curve values={g} w={props.w} top={top} bottom={bot}
                   floor={floor} />

            <For each={dots}>{(ms) => (
              <circle cx={xAt(ms / sh.spanMs)} cy={yAt(levelOf(sh, ms))} r="2.5"
                      fill="var(--on-uv)" />
            )}</For>

            {/* A/D/S/R under their segments -- skipped when the segment is
              * narrower than the letter, rather than shrunk. */}
            <For each={segs}>{([ch, from, to]) => {
              const x0 = xAt(from / sh.spanMs), x1 = xAt(to / sh.spanMs);
              return (to > from && x1 - x0 >= 12) ? (
                <text class="t-hint plot-caption" x={(x0 + x1) / 2} y={bot + LABEL_H - 1}
                      text-anchor="middle">{ch}</text>
              ) : null;
            }}</For>

            {/* The step edge, amber when the envelope runs past it. */}
            <line x1={xAt(sh.msStep / sh.spanMs)} x2={xAt(sh.msStep / sh.spanMs)}
                  y1={top} y2={bot} stroke-width="2"
                  stroke={sh.truncated ? 'var(--amber)' : 'var(--line-200)'} />

            <Axis w={props.w} y={props.h - INSET - AXIS_H} spanMs={sh.spanMs} markMs={sh.msStep} />
          </>
        );
      })()}
    </Well>
  );
}
