// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The three panels right of the ring: Gate, Envelope, Fade.
 *
 * THE TITLE RUNS UP THE LEFT EDGE, which is what pays for the third panel: a
 * heading above the knobs cost a 28px band that said one word.
 *
 * Laid out as the native editor's (TranceGateEditor.cpp, layoutPanels): four
 * cells a panel, every knob the same size; the Fade panel's knob in the first
 * cell and its two switches and two actions in two columns after it.
 */
import { Button, infoAttrs } from '@ultraviolet/ui';
import { ParamKnob, ParamToggle } from '@ultraviolet/ui/params';
import { P } from './msg.js';
import { INFO } from './info.js';

export function Panels(props) {
  const host = () => props.host;
  return (
    <div class="panels">
      {/* Rate, Length, Amount, Width -- the panel's own order. */}
      <section class="panel">
        <h2 class="t-title" {...infoAttrs(INFO.gatePanel)}>GATE</h2>
        <div class="knob-row">
          {/* One division an arrow: thirteen of them, the declaration's. */}
          <ParamKnob params={host()} idx={P.rate} label="Rate" steps={props.rateSteps}
                     info={INFO.rate} readoutInfo={INFO.readout.rate} />
          {/* Holds on half a bar to four bars at the Rate (the engine's list). */}
          <ParamKnob params={host()} idx={P.length} label="Length"
                     detents={props.lengthDetents}
                     info={INFO.length} readoutInfo={INFO.readout.length} />
          <ParamKnob params={host()} idx={P.amount} label="Amount"
                     info={INFO.amount} readoutInfo={INFO.readout.amount} />
          <ParamKnob params={host()} idx={P.width} label="Width"
                     info={INFO.width} readoutInfo={INFO.readout.width} />
        </div>
      </section>

      {/* The stages read in whichever unit Time asks for, and take either
        * typed (lib/stages.js). */}
      <section class="panel">
        <h2 class="t-title" {...infoAttrs(INFO.envelopePanel)}>ENVELOPE</h2>
        <div class="knob-row">
          <ParamKnob params={host()} idx={P.attack} label="Attack"
                     info={INFO.attack} readoutInfo={INFO.readout.attack} />
          <ParamKnob params={host()} idx={P.decay} label="Decay"
                     info={INFO.decay} readoutInfo={INFO.readout.decay} />
          <ParamKnob params={host()} idx={P.sustain} label="Sustain"
                     info={INFO.sustain} readoutInfo={INFO.readout.sustain} />
          <ParamKnob params={host()} idx={P.release} label="Release"
                     info={INFO.release} readoutInfo={INFO.readout.release} />
        </div>
      </section>

      {/* FADE: the knob, its direction and shape, and the order it introduces
        * steps in. The order controls live here because the order only means
        * anything to this knob. */}
      <section class="panel">
        <h2 class="t-title" {...infoAttrs(INFO.fadePanel)}>FADE</h2>
        <div class="knob-row">
          <ParamKnob params={host()} idx={P.fade} label="Fade"
                     info={INFO.fade} readoutInfo={INFO.readout.fade} />
          <div class="fade-switches">
            <ParamToggle params={host()} idx={P.fadeDir} label="Out" info={INFO.out} />
            <ParamToggle params={host()} idx={P.fadeSoft} label="Soft" info={INFO.soft} />
          </div>
          <div class="fade-actions">
            {/* The count is the whole affordance: a sequence is being typed,
              * and how far in you are. */}
            <Button on={props.orderMode} info={INFO.order} onClick={props.onOrder}>
              {props.orderMode ? `Set order ${props.orderNext}/${props.hits}` : 'Set order'}
            </Button>
            <Button info={INFO.shuffle} onClick={props.onShuffle}>Shuffle order</Button>
          </div>
        </div>
      </section>
    </div>
  );
}
