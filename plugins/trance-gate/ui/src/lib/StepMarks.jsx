// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the Trance Gate's plots draw that knows what a STEP is: the rules
 * between steps, their numbers, and the gate's curve over an Amount floor.
 *
 * The general half -- the well, the millisecond ruler, the min/max band -- is
 * the kit's (components/Plot.jsx).
 */
import { For } from 'solid-js';
import { INSET } from '@ultraviolet/ui';

/*
 * plot::steps, BOTH LAYERS, and they are separate because they are painted at
 * different times: the rules sit UNDER the audio so a transient is never
 * hidden by a rule, and the numbers go over it "so a number is never
 * swallowed by the audio".
 *
 * A rule marks the BOUNDARY between steps -- x = left + w*i for i = 1..n-1 --
 * which is why dividing the whole well by the step count put them in the
 * wrong place.
 */
const RULE_MIN = 6;      /* below this a rule per step is a comb */
const NUMBER_MIN = 14;   /* below this the numbers touch */

export function StepRules(props) {
  const cells = () => {
    const n = props.count;
    if (!(n >= 1) || props.w <= 0) return [];
    const w = (props.w - 2 * INSET) / n;
    const perStep = w >= RULE_MIN;
    const out = [];
    for (let i = 1; i < n; i++) {
      const bar = i % 16 === 0, beat = i % 4 === 0;
      /* Bars and beats survive at any density -- they are what keeps a long
       * pattern readable. */
      if (!perStep && !bar && !beat) continue;
      out.push({ x: INSET + w * i, bar });
    }
    return out;
  };
  return (
    <For each={cells()}>{(c) => (
      <line x1={c.x} x2={c.x} y1={props.top} y2={props.bottom}
            stroke={c.bar ? 'var(--line-200)' : 'var(--line-100)'} />
    )}</For>
  );
}

export function StepNumbers(props) {
  const cells = () => {
    const n = props.count;
    if (!(n >= 1) || props.w <= 0) return [];
    const w = (props.w - 2 * INSET) / n;
    if (w < NUMBER_MIN) return [];
    return Array.from({ length: n }, (_, i) => ({ i, x: INSET + w * i + 2 }));
  };
  return (
    <For each={cells()}>{(c) => (
      <text class="t-hint step-number" x={c.x} y={props.top + 10}>{c.i + 1}</text>
    )}</For>
  );
}


/*
 * ONE-SIDED, A FILLED REGION RISING FROM THE FLOOR.
 *
 * There were two renderers here: this one, and a MIRRORED `Gate` for drawing
 * the pattern's gate over the scope's waveform. The scope is a rolling window
 * of wall time now and carries no gate overlay, so the mirrored one is gone
 * with it.
 *
 * The distinction is worth keeping in mind if an overlay is ever wanted back:
 * using the mirrored renderer for the envelope is what once made the envelope
 * plot look like a waveform rather than an envelope.
 */

/*
 * plot Curve: `under` filled in uv-glow, the `hull` stroked in uv at rail
 * weight, and below `floor` everything is lit whatever the gate does --
 * because the gate never closes past Amount, and an envelope drawn as though
 * it reached zero would be lying about what you hear.
 */
export function Curve(props) {
  const geom = () => {
    const v = props.values;
    if (!v || v.length < 2) return null;
    const top = props.top, bot = props.bottom;
    const floor = Math.min(1, Math.max(0, props.floor ?? 0));
    /* m = floor + (1 - floor)*g is affine in g, so the floor is a squash of
     * the unit box onto the part of the plot above it. */
    const h = (bot - top) * (1 - floor);
    const x = (i) => INSET + (props.w - 2 * INSET) * (i / (v.length - 1));
    const y = (g) => top + h * (1 - Math.min(1, Math.max(0, g)));
    /* Array.from, not .map: the values may be a Float32Array, whose map
     * would return numbers. */
    const hull = Array.from(v, (g, i) => `${i ? 'L' : 'M'} ${x(i).toFixed(2)} ${y(g).toFixed(2)}`).join(' ');
    return {
      hull,
      under: `${hull} L ${x(v.length - 1).toFixed(2)} ${(top + h).toFixed(2)} L ${x(0).toFixed(2)} ${(top + h).toFixed(2)} Z`,
      floorY: top + h,
      hasFloor: floor > 0,
    };
  };
  return (
    <>
      {geom() && (
        <>
          {geom().hasFloor && (
            <rect x={INSET} y={geom().floorY} width={props.w - 2 * INSET}
                  height={Math.max(0, props.bottom - geom().floorY)}
                  fill="var(--uv-glow)" />
          )}
          <path d={geom().under} fill="var(--uv-glow)" stroke="none" />
          <path d={geom().hull} fill="none" stroke="var(--uv)" stroke-width="2" />
        </>
      )}
    </>
  );
}
