/*
 * The row under the panels: the slot, the pattern's settings, and the three
 * actions that replace the pattern rather than adjust it.
 * Copyright (c) 2026 Torben Gräber. MIT.
 */
import { Button, sendMessage } from '@ultraviolet/ui';
import { ParamSelect, ParamToggle } from '@ultraviolet/ui/params';
import { MSG, P } from './msg.js';
import { randomize } from './steps.js';

const SLOTS = ['1', '2', '3', '4', '5', '6', '7', '8'];
/* "%", not "% Step": the control's own name says what it is a percent of. */
const TIME_MODES = ['ms', '%'];
const CURVES = ['Linear', 'Exponential', 'S-Curve'];

export function SettingsRow(props) {
  return (
    <div class="settings-row">
      {/* No label: the StepGrid card puts the pattern Select above-left of the
        * grid, where its position says what it is. Named for a screen reader. */}
      <ParamSelect params={props.host} idx={P.slot} options={SLOTS} width={80} ariaLabel="Slot" />
      <ParamToggle params={props.host} idx={P.legato} label="Join Neighbors" />
      <ParamSelect params={props.host} idx={P.curve} options={CURVES} label="Curve"
                   labelWidth={44} width={124} />
      <ParamSelect params={props.host} idx={P.timeMode} options={TIME_MODES} label="Time"
                   labelWidth={36} width={88} />
      <span class="spacer" />
      <Button title="Fill this slot with a new pattern and arrival order"
              onClick={randomize}>RANDOM</Button>
      {/* Copy and paste as the design's joined icon pair: copy before paste,
        * sharing a hairline, each named in full for the pointer and the
        * screen reader. A message each: THE PLUGIN reads and writes the
        * clipboard, and the hint bar says how it went. */}
      <div class="btn-group">
        <Button icon="copy" title="Copy slot"
                onClick={() => sendMessage(MSG.copySlot)} />
        <Button icon="paste" title="Paste into slot"
                onClick={() => sendMessage(MSG.pasteSlot)} />
      </div>
    </div>
  );
}
