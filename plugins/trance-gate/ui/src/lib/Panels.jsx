/*
 * The three panels right of the ring: Gate, Envelope, Fade.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * THE TITLE RUNS UP THE LEFT EDGE, which is what pays for the third panel: a
 * heading above the knobs cost a 28px band that said one word.
 */
import { Button, infoAttrs } from '@ultraviolet/ui';
import { ParamKnob, ParamSelect, ParamToggle } from '@ultraviolet/ui/params';
import { P } from './msg.js';
import { INFO } from './info.js';

/* Which end the pattern is built up from. 100% is the pattern either way. */
const FADE_DIRS = ['In', 'Out'];

export function Panels(props) {
  const host = () => props.host;
  return (
    <div class="panels">
      {/* Rate, Length, Amount, Width -- the panel's own order. */}
      <section class="panel">
        <h2 class="t-title" {...infoAttrs(INFO.gatePanel)}>GATE</h2>
        <div class="knob-row">
          <ParamKnob params={host()} idx={P.rate} label="Rate"
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

      {/* The stages' readouts are the plugin's text in whichever unit Env Time
        * asks for, and it parses what is typed into them the same way. */}
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

      {/* FADE: the knob, its shape, and the order it introduces steps in. The
        * order controls live here because the order only means anything to
        * this knob. */}
      <section class="panel">
        <h2 class="t-title" {...infoAttrs(INFO.fadePanel)}>FADE</h2>
        <div class="knob-row">
          <ParamKnob params={host()} idx={P.fade} label="Fade"
                     info={INFO.fade} readoutInfo={INFO.readout.fade} />
          <div class="fade-actions">
            <ParamSelect params={host()} idx={P.fadeDir} options={FADE_DIRS} label="Dir"
                         labelWidth={28} width={76} info={INFO.dir} />
            <ParamToggle params={host()} idx={P.fadeSoft} label="Soft" info={INFO.soft} />
            {/* ORDER MODE. The count is the whole affordance: a sequence is
              * being typed, and how far in you are. */}
            <Button on={props.orderMode}
                    info={INFO.order}
                    onClick={props.onOrder}>
              {props.orderMode ? `ORDER ${props.orderNext}/${props.hits}` : 'ORDER'}
            </Button>
            <Button info={INFO.shuffle} onClick={props.onShuffle}>
              SHUFFLE
            </Button>
          </div>
        </div>
      </section>
    </div>
  );
}
