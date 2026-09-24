/*
 * Switch, select, glyph buttons, tabs and the hint bar.
 * Copyright (c) 2026 Torben Gräber. MIT.
 * Ported from UvLookAndFeel and UvComponents.
 */
import { For, createSignal, Show } from 'solid-js';
import { setParam, beginGesture, endGesture } from './iplug.js';

const commit = (idx, v) => { beginGesture(idx); setParam(idx, v); endGesture(idx); };

/*
 * THE SWITCH FORM: a 28x14 bg-200 housing with an 8x8 square at 2px that
 * moves to 16px and lights.
 *
 * The LED form (a round lens) is for state the plugin REPORTS; a
 * user-settable boolean like Join Neighbors is a switch. Getting it backwards
 * would say the plugin is telling you something when in fact it is asking.
 */
export function Switch(props) {
  const on = () => (props.value ?? 0) > 0.5;
  return (
    <div class="switch-row" onClick={() => commit(props.idx, on() ? 0 : 1)}>
      <div class="switch" classList={{ on: on() }}>
        <div class="switch-knob" classList={{ 'glow-led': on() }} />
      </div>
      <span class="switch-label t-label">{props.label}</span>
    </div>
  );
}

/*
 * A CHOICE, NOT A QUANTITY. The chevron is two 1px strokes rather than an
 * icon: the system uses no icon set, and this and the tie bar are its only
 * glyphs.
 */
export function Select(props) {
  const [open, setOpen] = createSignal(false);
  const n = () => props.options.length;
  const idx = () => Math.round((props.value ?? 0) * (n() - 1));
  return (
    /* The label is BESIDE the select, on the same 28px row -- these say how
     * the envelope is measured or drawn rather than what its values are, and
     * the envelope panel's four knobs leave no room for either. */
    <div class="select-group">
      <Show when={props.label}>
        <span class="select-label t-label" style={{ width: `${props.labelWidth ?? 44}px` }}>
          {props.label}
        </span>
      </Show>
      <div class="select" classList={{ open: open() }}
           style={{ width: `${props.width ?? 124}px` }}>
        <span class="select-value t-value">{props.options[idx()]}</span>
        <svg class="chevron" width="8" height="6" viewBox="0 0 8 6">
          <path d="M1 1 L4 4 L7 1" fill="none" stroke="var(--ink-muted)" stroke-width="1" />
        </svg>
        <select
          value={idx()}
          onFocus={() => setOpen(true)} onBlur={() => setOpen(false)}
          onChange={(e) => { commit(props.idx, n() > 1 ? +e.currentTarget.value / (n() - 1) : 0); setOpen(false); }}
        >
          <For each={props.options}>{(o, i) => <option value={i()}>{o}</option>}</For>
        </select>
      </div>
    </div>
  );
}

/* A 12px glyph centred in the button: two offset rectangles for copy, and a
 * sheet under a clipboard's tab for paste. Hairlines, like the caret. */
export function GlyphButton(props) {
  return (
    <button class="glyph" title={props.title} onClick={props.onClick}>
      <Show
        when={props.glyph === 'copy'}
        fallback={
          <svg width="14" height="14" viewBox="0 0 14 14">
            <rect x="2" y="3" width="10" height="9" fill="none" stroke="currentColor" stroke-width="1" />
            <rect x="5" y="0.5" width="4" height="3" fill="none" stroke="currentColor" stroke-width="1" />
          </svg>
        }>
        <svg width="14" height="14" viewBox="0 0 14 14">
          <rect x="1.5" y="1.5" width="8" height="8" fill="none" stroke="currentColor" stroke-width="1" />
          <rect x="4.5" y="4.5" width="8" height="8" fill="none" stroke="currentColor" stroke-width="1" />
        </svg>
      </Show>
    </button>
  );
}

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

/* Three clauses at most. Truncating rather than shrinking the text is
 * deliberate: a fourth clause means the window needs simplifying, not a
 * smaller font. */
export function HintBar(props) {
  return (
    <div class="hint-bar t-hint">
      <For each={props.clauses.slice(0, 3)}>{(c, i) => (
        <>
          <Show when={i() > 0}><span class="sep">–</span></Show>
          <span class="hint-key">{c[0]}</span>
          <span class="hint-val">{c[1]}</span>
        </>
      )}</For>
    </div>
  );
}
