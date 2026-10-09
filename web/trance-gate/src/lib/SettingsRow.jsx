// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The row under the panels: the slot, how the rest is read, and the one
 * action that replaces the pattern rather than adjusting it.
 *
 * The native editor's row (TranceGateEditor.cpp, layoutRow), with Randomize at
 * its end: in Live that is one of the window verbs in the column right of the
 * panels, beside copy, paste, export and import. None of those four can work
 * in the Move's page -- it has no clipboard and no downloads -- so the column
 * is not drawn, and the verb that is left sits where the web editor had it.
 */
import { Button } from '@ultraviolet/ui';
import { ParamSelect, ParamToggle } from '@ultraviolet/ui/params';
import { P } from './msg.js';
import { randomize } from './steps.js';
import { INFO } from './info.js';

const SLOTS = ['1', '2', '3', '4', '5', '6', '7', '8'];
const CURVES = ['Linear', 'Exponential', 'S-Curve'];

export function SettingsRow(props) {
  return (
    <div class="settings-row">
      {/* No label: it sits above-left of the pads, where the StepGrid card
        * puts a pattern select. Named for a screen reader. */}
      <ParamSelect params={props.host} idx={P.slot} options={SLOTS} width={80} ariaLabel="Slot"
                   info={INFO.slot} />
      <ParamToggle params={props.host} idx={P.legato} label="Join Neighbors"
                   info={INFO.join} />
      {/* 128, so "Exponential" is whole (the native editor's figure). */}
      <ParamSelect params={props.host} idx={P.curve} options={CURVES} label="Curve"
                   labelWidth={44} width={128} info={INFO.curve} />
      <ParamToggle params={props.host} idx={P.timeMode} label="Time in %"
                   info={INFO.time} />
      <span class="spacer" />
      <Button info={INFO.randomize} onClick={randomize}>Randomize</Button>
    </div>
  );
}
