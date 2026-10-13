// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The band's plot: the Pattern, the gate across one cycle as the engine
 * applies it.
 *
 * In Live the band holds a second plot, the Signal -- the audio before and
 * after the gate -- with a tab strip to switch. No audio reaches a page on the
 * Move, so there is no Signal to show and no tab to switch to; the Pattern has
 * the band to itself, the full width of the pads.
 *
 * THE VIEWBOX IS THE PIXEL SIZE, 1:1: a stretched viewBox scales the text too.
 * The well and the ruler are the kit's.
 */
import { createMemo } from 'solid-js';
import { Well, INSET, CAPTION } from '@ultraviolet/ui';
import { StepRules, StepNumbers, Curve, Rule } from './StepMarks.jsx';

/*
 * THE CURVE IS THE ENGINE'S, NOT A DESCRIPTION OF IT: one cycle rendered
 * through a scratch engine with a DC input (lib/renders.js), so ties, legato,
 * the per-step level, the fade and a release crossing three steps all arrive
 * correct because nothing here reasons about them.
 *
 * AMOUNT IS THE ONE THING APPLIED HERE, because it can be:
 *
 *     m = 1 - amount*(1 - g) = floor + (1 - floor)*g,   floor = 1 - amount
 *
 * is affine in g, so it is a floor under the curve rather than a reason to
 * render again. The render leaves it at 1.
 */
export function PatternPlot(props) {
  const n = () => Math.max(1, props.length ?? 16);
  const values = createMemo(() => props.gate?.values ?? []);
  const top = CAPTION, bot = () => props.h - INSET;
  /* THE PLAYHEAD IN WHOLE STEPS, at the step's leading edge, as the ring and
   * the pads have it -- Ultraviolet 1.1's Motion, and the native editor's. */
  const headX = () => INSET + (props.w - 2 * INSET) * (props.playStep / n());
  return (
    <Well w={props.w} h={props.h} info={props.info} caption="PATTERN   ONE CYCLE">
      {/* Rules UNDER the curve, so nothing is hidden by a rule. */}
      <StepRules count={n()} w={props.w} top={top} bottom={bot()} />
      <Curve values={values()} w={props.w} top={top} bottom={bot()}
             floor={1 - Math.min(1, Math.max(0, props.amount ?? 1))} />
      {props.moving && props.playStep >= 0 && (
        <Rule class="playhead" x={headX()} top={top} bottom={bot()} />
      )}
      {/* Numbers LAST, so a number is never swallowed by what it labels. */}
      <StepNumbers count={n()} w={props.w} top={top} />
    </Well>
  );
}
