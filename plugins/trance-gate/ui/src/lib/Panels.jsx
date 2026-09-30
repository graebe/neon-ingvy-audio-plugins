/*
 * The three panels right of the ring: Gate, Envelope, Fade.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * THE TITLE RUNS UP THE LEFT EDGE, which is what pays for the third panel: a
 * heading above the knobs cost a 28px band that said one word.
 */
import { Button } from '@ultraviolet/ui';
import { ParamKnob, ParamSelect, ParamToggle } from '@ultraviolet/ui/params';
import { P } from './msg.js';

/* Which end the pattern is built up from. 100% is the pattern either way. */
const FADE_DIRS = ['In', 'Out'];

export function Panels(props) {
  const host = () => props.host;
  return (
    <div class="panels">
      {/* Rate, Length, Amount, Width -- the panel's own order. */}
      <section class="panel">
        <h2 class="t-title">GATE</h2>
        <div class="knob-row">
          <ParamKnob params={host()} idx={P.rate} label="Rate" />
          <ParamKnob params={host()} idx={P.length} label="Length" />
          <ParamKnob params={host()} idx={P.amount} label="Amount" />
          <ParamKnob params={host()} idx={P.width} label="Width" />
        </div>
      </section>

      <section class="panel">
        <h2 class="t-title">ENVELOPE</h2>
        <div class="knob-row">
          <ParamKnob params={host()} idx={P.attack} label="Attack" display={props.stageText(P.attack)} />
          <ParamKnob params={host()} idx={P.decay} label="Decay" display={props.stageText(P.decay)} />
          <ParamKnob params={host()} idx={P.sustain} label="Sustain" />
          <ParamKnob params={host()} idx={P.release} label="Release" display={props.stageText(P.release)} />
        </div>
      </section>

      {/* FADE: the knob, its shape, and the order it introduces steps in. The
        * order controls live here because the order only means anything to
        * this knob. */}
      <section class="panel">
        <h2 class="t-title">FADE</h2>
        <div class="knob-row">
          <ParamKnob params={host()} idx={P.fade} label="Fade" />
          <div class="fade-actions">
            <ParamSelect params={host()} idx={P.fadeDir} options={FADE_DIRS} label="Dir"
                         labelWidth={28} width={76} />
            <ParamToggle params={host()} idx={P.fadeSoft} label="Soft" />
            {/* ORDER MODE. The count is the whole affordance: a sequence is
              * being typed, and how far in you are. */}
            <Button on={props.orderMode}
                    title="Tap the steps in the order the fade should introduce them"
                    onClick={props.onOrder}>
              {props.orderMode ? `ORDER ${props.orderNext}/${props.hits}` : 'ORDER'}
            </Button>
            <Button title="Shuffle the arrival order" onClick={props.onShuffle}>
              SHUFFLE
            </Button>
          </div>
        </div>
      </section>
    </div>
  );
}
