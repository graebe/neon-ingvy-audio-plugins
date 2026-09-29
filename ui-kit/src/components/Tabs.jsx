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
 *
 * THE GLOW IS A CSS RULE ON `.tab.on`, NOT A CLASS TOGGLED FROM HERE.
 *
 * It used to be a second entry in this classList -- `'glow-led': active` -- and
 * the halo did not keep up with the click: the fill and the text swapped at
 * once and the glow arrived late or not until the next unrelated repaint. Two
 * class toggles in one commit are not two paints, and a `filter` appearing on
 * an element is the half a WKWebView is content to defer.
 *
 * Every other lit thing in this UI already does it the other way -- `.pad.on`
 * carries `filter: var(--glow-led)` in the stylesheet -- so this was the odd
 * one out as well as the broken one. One class, one rule, one paint.
 */
export function Tabs(props) {
  return (
    <div class="tabs">
      <For each={props.tabs}>{(t, i) => (
        <button class="tab" classList={{ on: props.active === i() }}
                onClick={() => props.onSelect(i())}>
          <span class="tab-text t-hint">{t}</span>
        </button>
      )}</For>
    </div>
  );
}
