// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The band: the Pattern and the Signal plots in one place, one at a time, and
 * the tab strip laid over the plot's right edge.
 */
import { createSignal, Show } from 'solid-js';
import { Tabs } from '@ultraviolet/ui';
import { PatternPlot, Scope } from './Plots.jsx';
import { INFO } from './info.js';

/* Sixteen 40px pads with 8px between them: the pads decide the content width. */
export const PLOT_W = 760;

export function Band(props) {
  const [tab, setTab] = createSignal(0);
  return (
    <div class="band">
      <div class="band-plot">
        {/* The fractional phase: this playhead is a line and glides, where a lit
          * pad is discrete and snaps. Both read the one clock. */}
        <Show when={tab() === 0}>
          <PatternPlot length={props.ui.length} steps={props.ui.steps}
                       ties={props.ui.ties} depths={props.ui.depths}
                       weights={props.weights} gate={props.gate}
                       params={props.params} w={PLOT_W} h={92} info={INFO.patternPlot}
                       phase={props.phase} moving={props.ui.moving} />
        </Show>
        {/* The Scope's x-axis is one cycle of the gate, so the step rules line
          * up with it and the envelope can be drawn over it. */}
        <Show when={tab() === 1}>
          <Scope scope={props.scope} w={PLOT_W} h={92} info={INFO.signalPlot}
                 windowMs={props.scopeWindow} head={props.scopeHead}
                 length={props.ui.length} steps={props.ui.steps}
                 ties={props.ui.ties} depths={props.ui.depths}
                 weights={props.weights} params={props.params}
                 gate={props.gate}
                 phase={props.phase} moving={props.ui.moving} />
        </Show>
      </div>
      <Tabs tabs={['Pattern', 'Signal']} infos={[INFO.patternTab, INFO.signalTab]}
            active={tab()} onSelect={setTab} />
    </div>
  );
}
