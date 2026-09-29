/*
 * The window's one shared control: the hint bar.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Lifted from the Trance Gate's Controls.jsx, which has nine of these. Only this
 * one is carried over, because a component copied "for later" is a component
 * nobody maintains -- the rest arrive when this window grows something to put in
 * them.
 */
import { createSignal, For } from 'solid-js';

/*
 * "Every window states its own conventions once, in the Hint bar, in the pattern
 * VERB IN ink, REST IN ink-muted, separated by en dashes."
 *
 * This window has no interaction to state, so its clauses state what the picture
 * IS instead -- the axis, the floor and the window length. A reader cannot get
 * those from the drawing, and an analyzer whose scale is a guess is decoration.
 */
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

/*
 * A BUTTON, from the design system's own `.ph-btn`: 28px tall (control-h),
 * space-3 padding, a bg-200 well on a line-200 border, rising to bg-300 under
 * the pointer.
 *
 * `on` is the system's PRIMARY state -- uv fill, on-uv text, glow-led -- and it
 * is the same state the system gives a pressed button, because "there is no
 * separate selected colour: selected is uv". A latched button that is engaged
 * and a momentary button that is held look alike on purpose.
 *
 * No icon. "The system uses no icon set: state is shown by light and by words.
 * Do not add icons for play, copy or settings; write the word."
 */
export function Button(props) {
  return (
    <button
      type="button"
      class="btn t-button"
      classList={{ on: props.on }}
      onClick={props.onClick}
    >
      {props.children}
    </button>
  );
}

/*
 * A SELECT, lifted from the Trance Gate's editor along with its CSS.
 *
 * The mechanism is the part worth keeping: a styled div with a REAL, invisible
 * <select> stretched over it. The popup is then the OS's -- which is the only
 * kind that behaves correctly inside a plugin's WebView, where a div-based menu
 * would be clipped by the window and would not follow the pointer out of it.
 *
 * The one change from the original: it takes an index and an onChange rather
 * than a normalised parameter, because this plugin has no parameters to bind to.
 */
export function Select(props) {
  const [open, setOpen] = createSignal(false);
  return (
    <div class="select" classList={{ open: open() }}
         style={{ width: `${props.width ?? 96}px` }}>
      <span class="select-value t-value">{props.options[props.value ?? 0]}</span>
      <svg class="chevron" width="8" height="6" viewBox="0 0 8 6">
        <path d="M1 1 L4 4 L7 1" fill="none" stroke="var(--ink-muted)" stroke-width="1" />
      </svg>
      <select
        value={props.value ?? 0}
        onFocus={() => setOpen(true)}
        onBlur={() => setOpen(false)}
        onChange={(e) => { props.onChange?.(+e.currentTarget.value); setOpen(false); }}
      >
        <For each={props.options}>{(o, i) => <option value={i()}>{o}</option>}</For>
      </select>
    </div>
  );
}
