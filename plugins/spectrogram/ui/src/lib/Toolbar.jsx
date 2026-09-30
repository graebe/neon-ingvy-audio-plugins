/*
 * The first row: how the picture is drawn -- the zoom, the bar view, pause --
 * and the window's one amber word when nothing is arriving.
 * Copyright (c) 2026 Torben Gräber. MIT.
 */
import { Button, Select, Toggle } from '@ultraviolet/ui';
import { RANGES } from './columns.js';

export const BARS = [1, 2, 4, 8, 16];

export function Toolbar(props) {
  return (
    <div class="toolbar">
      {/* The window's single amber mark, and only when it means something.
        * The plugin's name is not here: the host shows it. */}
      <span class="t-label stale">{props.live ? '' : 'no signal'}</span>
      <div class="toolbar-actions">
        {/* No label beside it: the option names the band and the hint bar
            below already prints the numbers. */}
        <Select ariaLabel="Range" options={RANGES.map((r) => r.name)} value={props.range}
                onChange={props.onRange} />
        {/* The switch says WHICH axis; the dropdown says how much of it. It
            stays visible while off, so the window does not change shape. */}
        <Toggle label="bars" value={props.bars} onChange={props.onBars} />
        <Select ariaLabel="Bars" options={BARS.map(String)} value={props.barCount}
                onChange={props.onBarCount} width={64} />
        <Button on={props.paused} onClick={props.onPause}>Pause</Button>
      </div>
    </div>
  );
}
