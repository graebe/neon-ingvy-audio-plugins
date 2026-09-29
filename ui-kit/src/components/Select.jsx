/*
 * A choice, not a quantity. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * THE MECHANISM IS THE PART WORTH KEEPING: a styled div with a REAL, invisible
 * <select> stretched over it. The popup is then the OS's -- which is the only
 * kind that behaves correctly inside a plugin's WebView, where a div-based menu
 * would be clipped by the window and would not follow the pointer out of it.
 *
 * The chevron is two 1px strokes rather than an icon: the system uses no icon
 * set, and this is one of its two glyphs.
 *
 * IT TAKES AN INDEX, NOT A PARAMETER. The Trance Gate's version was bound to a
 * host parameter and converted normalised values in and out; the Spectrogram's
 * fork took an index because it has no parameters to bind to. The index form is
 * the one that belongs in a kit -- a Select is a choice widget, and knowing
 * what a choice MEANS is the caller's business. See ParamSelect in the Trance
 * Gate's editor for the binding.
 */
import { For, Show, createSignal } from 'solid-js';

export function Select(props) {
  const [open, setOpen] = createSignal(false);
  return (
    /* The label sits BESIDE the select, on the same 28px row -- these say how a
     * thing is measured or drawn rather than what its value is, and a panel of
     * knobs leaves no room above. */
    <div class="select-group">
      <Show when={props.label}>
        <span class="select-label t-label" style={{ width: `${props.labelWidth ?? 44}px` }}>
          {props.label}
        </span>
      </Show>
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
    </div>
  );
}
