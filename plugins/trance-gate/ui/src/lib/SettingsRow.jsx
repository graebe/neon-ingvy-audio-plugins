/*
 * The row under the panels: the slot, the pattern's settings, and the three
 * actions that replace the pattern rather than adjust it.
 * Copyright (c) 2026 Torben Gräber. MIT.
 */
import { createSignal, Show } from 'solid-js';
import { Button, sendMessage } from '@ultraviolet/ui';
import { ParamSelect, ParamToggle } from '@ultraviolet/ui/params';
import { MSG, P } from './msg.js';
import { randomize } from './steps.js';
import { modKey } from './clipboard.js';

const SLOTS = ['1', '2', '3', '4', '5', '6', '7', '8'];
/* "%", not "% Step": the control's own name says what it is a percent of. */
const TIME_MODES = ['ms', '%'];
const CURVES = ['Linear', 'Exponential', 'S-Curve'];

export function SettingsRow(props) {
  const [pasting, setPasting] = createSignal(false);
  let pasteEl;

  /*
   * PASTE: tried programmatically first, so on a host that grants it no field
   * appears; otherwise a field the user pastes into, because the PASTE EVENT
   * carries the data with no permission at all.
   */
  const startPaste = async () => {
    try {
      const t = await navigator.clipboard.readText();
      if (t) { sendMessage(MSG.patch, t); return; }
    } catch { /* no permission -- the field below is the answer */ }
    setPasting(true);
    requestAnimationFrame(() => pasteEl?.focus());
  };

  return (
    <div class="settings-row">
      {/* No label: the StepGrid card puts the pattern Select above-left of the
        * grid, where its position says what it is. Named for a screen reader. */}
      <ParamSelect params={props.host} idx={P.slot} options={SLOTS} width={96} ariaLabel="Slot" />
      <ParamToggle params={props.host} idx={P.legato} label="Join Neighbors" />
      <ParamSelect params={props.host} idx={P.curve} options={CURVES} label="Curve"
                   labelWidth={44} width={124} />
      <ParamSelect params={props.host} idx={P.timeMode} options={TIME_MODES} label="Time"
                   labelWidth={36} width={88} />
      <span class="spacer" />
      <Button title="Fill this slot with a new pattern and arrival order"
              onClick={randomize}>RANDOM</Button>
      <Button icon title="Copy gate config"
              onClick={() => sendMessage(MSG.requestPatch)}>
        <svg width="14" height="14" viewBox="0 0 14 14">
          <rect x="1.5" y="1.5" width="8" height="8" fill="none" stroke="currentColor" stroke-width="1" />
          <rect x="4.5" y="4.5" width="8" height="8" fill="none" stroke="currentColor" stroke-width="1" />
        </svg>
      </Button>
      <Button icon title="Paste gate config" onClick={startPaste}>
        <svg width="14" height="14" viewBox="0 0 14 14">
          <rect x="2" y="3" width="10" height="9" fill="none" stroke="currentColor" stroke-width="1" />
          <rect x="5" y="0.5" width="4" height="3" fill="none" stroke="currentColor" stroke-width="1" />
        </svg>
      </Button>
      <Show when={pasting()}>
        <input ref={pasteEl} class="paste-field t-hint"
               placeholder={`${modKey()}V to paste`}
               onPaste={(e) => {
                 const t = e.clipboardData?.getData('text');
                 if (t) sendMessage(MSG.patch, t);
                 setPasting(false);
                 e.preventDefault();
               }}
               onBlur={() => setPasting(false)}
               onKeyDown={(e) => { if (e.key === 'Escape') setPasting(false); }} />
      </Show>
    </div>
  );
}
