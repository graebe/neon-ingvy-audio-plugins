/*
 * The band's two plots: the Pattern (the gate across one cycle, as the engine
 * applies it) and the Signal (the capture, dry behind and gated in front).
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * THE VIEWBOX IS THE PIXEL SIZE, 1:1: a stretched viewBox scales the text too.
 * The well, the ruler and the band are the kit's.
 */
import { createMemo } from 'solid-js';
import { Well, Axis, band, INSET, CAPTION } from '@ultraviolet/ui';
import { StepRules, StepNumbers, Curve } from './StepMarks.jsx';

/*
 * THE PATTERN: the gain the engine will apply across one CYCLE -- the same
 * envelope, once per lit step, scaled by that step's amount. Drawn as the
 * gate rather than as bars, because it is the same quantity the envelope plot
 * shows and reading them against each other is the point of the two views.
 */
export function PatternPlot(props) {
  const n = () => Math.max(1, props.length ?? 16);
  /*
   * THE CURVE IS THE ENGINE'S, NOT A DESCRIPTION OF IT.
   *
   * This used to walk the pattern here and rebuild the envelope from the
   * parameters -- a second implementation of the DSP, and the only one in this
   * editor with no oracle behind it, which is exactly why it was wrong. An OFF
   * step pushed hard zeros and threw the carry away, so a release outliving its
   * step was drawn as an instant cut at the pad edge and the next step's attack
   * started from silence instead of the tail it should have continued from.
   * Release runs to 200% of the gate's Width; that is most settings, not an
   * edge case.
   *
   * The plugin renders one cycle through a scratch engine with a DC input and
   * pushes the samples. There is no model left here to be wrong -- ties,
   * legato, the per-step level, the fade and a release crossing three steps all
   * arrive correct because they are not being reasoned about.
   *
   * AMOUNT IS THE ONE THING STILL APPLIED HERE, because it can be:
   *
   *     m = 1 - amount*(1 - g) = floor + (1 - floor)*g,   floor = 1 - amount
   *
   * is affine in g, so it is a floor under the curve rather than a reason to
   * ask the plugin for a new one. Dragging Amount costs a repaint and nothing
   * else, which is why the render leaves it at 1.
   */
  const values = createMemo(() => props.gate?.values ?? []);
  const top = CAPTION, bot = () => props.h - INSET;
  return (
    <Well w={props.w} h={props.h} info={props.info} caption="PATTERN   ONE CYCLE">
      {/* Rules UNDER the curve, so nothing is hidden by a rule. */}
      <StepRules count={n()} w={props.w} top={top} bottom={bot()} />
      <Curve values={values()} w={props.w} top={top} bottom={bot()}
             floor={1 - Math.min(1, Math.max(0, props.params?.amount ?? 1))} />
      {/* THE FRACTIONAL PHASE, so this glides rather than stepping.
        * `phase / n` and not `(step + 0.5) / n`: the phase already carries its
        * position WITHIN the step, where an integer step index had to be
        * centred in its cell to sit anywhere sensible. */}
      {props.moving && (props.phase ?? -1) >= 0 && (
        <line class="playhead" stroke="var(--ink)"
              x1={INSET + (props.w - 2 * INSET) * (props.phase / n())}
              x2={INSET + (props.w - 2 * INSET) * (props.phase / n())}
              y1={top} y2={bot()} />
      )}
      {/* Numbers LAST, so a number is never swallowed by what it labels. */}
      <StepNumbers count={n()} w={props.w} top={top} />
    </Well>
  );
}

/*
 * THE SIGNAL. Dry behind in grey, gated in front in uv, the envelope over both.
 *
 * The dry is CONTEXT, NOT THE SUBJECT, so it is drawn back at partial alpha:
 * a sustained input fills every column edge to edge, and at full strength it
 * is a solid slab with the gated trace fighting to be seen through it.
 */
export function Scope(props) {
  /*
   * A SWEEP OF THE PATTERN, ON A STATIC AXIS, FILLING LEFT TO RIGHT.
   *
   * Column k is pattern phase k/cols and is drawn at that x -- always, whatever
   * the sweep is doing -- so the axis stands still, the step rules line up with
   * the gate, and the ENVELOPE can be drawn over the trace it produced. `head`
   * is where the plugin is writing, so the picture reads as filling rather than
   * scrolling.
   *
   * THIS WENT BACK AND FORTH ONCE, so here is the part that matters. It was a
   * pattern sweep, became a rolling window of wall time, and is a pattern sweep
   * again. The complaint that moved it was real: a column was refreshed once per
   * CYCLE -- seconds apart at sixteen steps -- so turning the input up made the
   * picture creep after it, which reads as a filtered meter.
   *
   * But the mapping was never the cause. The plugin published a column only when
   * it was COMPLETE, so the newest data was up to a column old and, far worse, a
   * level change was invisible until the sweep came round. It publishes the
   * partial column every block now: current to within one buffer, overwritten
   * and never blended. Wall time solved the symptom and cost the alignment --
   * step numbers and the gate overlay both came off the plot with it -- where
   * fixing the publish keeps both.
   */
  const AXIS_H = 14;
  const top = CAPTION, bot = () => props.h - INSET - AXIS_H;
  const mid = () => (top + bot()) / 2;
  const cycleMs = () => props.windowMs ?? 1000;
  const n = () => Math.max(1, props.length ?? 16);
  const x01 = (t) => INSET + (props.w - 2 * INSET) * Math.max(0, Math.min(1, t));

  /* One screen pixel per column of output, min/max over every capture column
   * behind it -- the kit's `band`, which the Side-Chain's well draws with too. */
  const geom = () => ({ x0: INSET, w: Math.max(1, Math.round(props.w - 2 * INSET)),
                        top, bottom: bot() });

  /*
   * THE ENVELOPE OVER THE TRACE, and it is drawn as the OUTLINE the gate cuts
   * rather than as a filled curve.
   *
   * The trace is already a filled band; a second fill over it is mud. So this is
   * the gate's shape mirrored about the zero line -- the envelope above and its
   * negative below, which is exactly the outline a symmetrical signal at full
   * level would fill. Laid over the real trace, the gap between them IS the
   * difference between what the gate asked for and what the audio did.
   *
   * It needs an axis that is the pattern to mean anything, which is why it was
   * taken off when the axis was wall time and why it can come back now.
   *
   * AND IT IS THE SAME RENDERED CURVE THE PATTERN TAB DRAWS. It used to be a
   * coarser re-derivation here -- a JS gate per step with no carry-in at all --
   * so the two tabs could disagree about the same gate. One curve, from the
   * engine, drawn twice.
   */
  const gate = createMemo(() => {
    const g = props.gate;
    if (!g || !g.values?.length) return null;
    const half = (bot() - top) * 0.5;
    /* Amount floors the gate exactly as it does in the Pattern plot, and for
     * the same reason: the render leaves it at 1 because it is affine. */
    const floor = 1 - Math.min(1, Math.max(0, props.params?.amount ?? 1));
    const up = [], dn = [];
    const N = g.values.length;
    for (let i = 0; i < N; i++) {
      const v = floor + (1 - floor) * g.values[i];
      const px = x01(i / N);
      up.push(`${up.length ? 'L' : 'M'} ${px.toFixed(1)} ${(mid() - half * v).toFixed(1)}`);
      dn.push([px, mid() + half * v]);
    }
    if (!up.length) return null;
    return { up: up.join(' '),
             dn: 'M ' + dn.map(([px, py]) => `${px.toFixed(1)} ${py.toFixed(1)}`).join(' L ') };
  });

  return (
    <Well w={props.w} h={props.h} info={props.info}
          caption={`SIGNAL   ONE CYCLE, ${Math.round(cycleMs())} MS   DRY IN GREY, GATED IN FRONT`}>
      {/* The step boundaries, UNDER everything: the axis is the pattern again, so
        * a column has a step and the rules say which. */}
      <StepRules count={n()} w={props.w} top={top} bottom={bot()} />
      {/* The zero line, so a silent stretch reads as silence rather than as a
        * gap in the drawing. */}
      <line x1={INSET} x2={props.w - INSET} y1={mid()} y2={mid()} stroke="var(--line-100)" />
      {/* The dry is CONTEXT, NOT THE SUBJECT, so it is drawn back at partial
        * alpha: a sustained input fills every column edge to edge, and at
        * full strength it is a slab with the gated trace fighting to be seen
        * through it. */}
      <path d={band(props.scope, 0, 1, geom())} fill="var(--plot-dry)" opacity="0.5" />
      {/*
        * THE GATED TRACE IS THE SUBJECT, so it gets the arc halo -- the same
        * 3px falloff the knob's value arc uses, and for the same reason: `uv`
        * is a near-white and a near-white drawn alone has no cast at all. The
        * 10px led halo is too wide here; on a trace that fills the well it
        * blooms into mud.
        */}
      <g class="glow-arc"><path d={band(props.scope, 2, 3, geom())} fill="var(--uv)" /></g>
      {/* The envelope, as an outline above and below zero. `ink-dim` and a
        * hairline: it is the reference the trace is measured against, so it must
        * not compete with it. */}
      {gate() && <>
        <path d={gate().up} fill="none" stroke="var(--ink-dim)" stroke-width="1" />
        <path d={gate().dn} fill="none" stroke="var(--ink-dim)" stroke-width="1" />
      </>}
      {/*
        * WHERE THE SWEEP IS WRITING. Not a playhead -- it is the same position,
        * but what it means here is "the picture is current up to here and stale
        * after it", which is the one thing a static axis cannot say on its own.
        */}
      {props.moving && (props.head ?? -1) >= 0 && props.scope?.count > 1 && (
        <line class="sweep" stroke="var(--uv)" opacity="0.7"
              x1={x01(props.head / props.scope.count)}
              x2={x01(props.head / props.scope.count)}
              y1={top} y2={bot()} />
      )}
      {/* Milliseconds into the cycle, which is what the axis now is. The same
        * axis the envelope plot uses, so the two read alike. */}
      <Axis w={props.w} y={props.h - INSET - AXIS_H} spanMs={cycleMs()} markMs={0} />
      {/* Numbers LAST, so a number is never swallowed by what it labels. */}
      <StepNumbers count={n()} w={props.w} top={top} />
    </Well>
  );
}
