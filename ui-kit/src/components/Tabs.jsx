/*
 * Tabs. Copyright (c) 2026 Torben Gräber. MIT.
 */
import { For } from 'solid-js';

/*
 * A VERTICAL STRIP ON THE BAND'S RIGHT EDGE, 24px wide, each tab a block of
 * the height divided evenly -- `band.removeFromRight(UvTabs::width)`.
 *
 * The text is rotated a quarter turn so it reads BOTTOM-TO-TOP, which is the
 * way a tab on a right edge is read. The lit one is a uv fill with onUv text
 * and glow-led, like every other lit thing in the system.
 */
export function Tabs(props) {
  return (
    <div class="tabs">
      <For each={props.tabs}>{(t, i) => (
        <button class="tab" classList={{ on: props.active === i(), 'glow-led': props.active === i() }}
                onClick={() => props.onSelect(i())}>
          <span class="tab-text t-hint">{t}</span>
        </button>
      )}</For>
    </div>
  );
}
