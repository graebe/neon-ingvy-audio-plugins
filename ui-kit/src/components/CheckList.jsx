/*
 * A dropdown that chooses SEVERAL. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * WHY THIS IS NOT A Select.
 *
 * Select is deliberately a real, invisible <select> stretched over a styled div,
 * because "the popup is then the OS's -- which is the only kind that behaves
 * correctly inside a plugin's WebView". Adding `multiple` to it throws that
 * away entirely: the element stops being a popup and becomes an inline list
 * box, it has no checkboxes, and it selects with ctrl-click, which is a hostile
 * thing to ask of anyone inside a plugin window.
 *
 * So this is the system's own answer instead, and the shape is already
 * specified: design/files/project/components/Select/README.md describes the open
 * list as "a bg-100 box with a uv border, one row per option, the current option
 * in uv, the row under the pointer bg-300". That is what the panel below is.
 *
 * IT IS LAID OUT INSIDE THE WINDOW, never portalled to <body>. The window does
 * not scroll and has a fixed size, so a menu that escaped its parent would be
 * clipped by the frame with no way to reach the rest of it. Absolute inside
 * `main` means the panel is always somewhere the window can show.
 *
 * A ROW IS A Toggle, because that is what the system says a user-settable
 * boolean is -- "the LED form is for state the plugin REPORTS; a user-settable
 * boolean is a switch" -- and because the iconography rule forbids inventing a
 * checkmark: "never invent a glyph outside the set. A control that has no glyph
 * in the set gets a word."
 */
import { Index, Show, createSignal, onCleanup } from 'solid-js';
import { Toggle } from './Toggle.jsx';
import { Icon } from './Icon.jsx';

export function CheckList(props) {
  /* props: label, summary, options [{ id, name, hint, disabled }],
   *        selected (array of id), onChange(array of id), width */
  const [open, setOpen] = createSignal(false);

  const isOn = (id) => (props.selected ?? []).includes(id);
  const toggle = (id, on) => {
    const now = (props.selected ?? []).filter((x) => x !== id);
    props.onChange?.(on ? [...now, id] : now);
  };

  /*
   * A CLICK ANYWHERE ELSE CLOSES IT. Without this the panel stays open behind
   * the next thing the user does, and in a window this small that is most of
   * the picture covered by a list they have finished with.
   */
  const away = (e) => {
    if (!host || host.contains(e.target)) return;
    setOpen(false);
  };
  let host;
  document.addEventListener('pointerdown', away);
  onCleanup(() => document.removeEventListener('pointerdown', away));

  return (
    <div class="checklist" ref={host}>
      <Show when={props.label}>
        <span class="select-label t-label">{props.label}</span>
      </Show>
      <button
        type="button"
        class="select checklist-face"
        classList={{ open: open() }}
        style={{ width: `${props.width ?? 116}px` }}
        aria-expanded={open()}
        onClick={() => setOpen((o) => !o)}
      >
        <span class="select-value t-value">{props.summary}</span>
        <Icon name="chevron" />
      </button>

      <Show when={open()}>
        <div class="checklist-panel">
          <Show
            when={(props.options ?? []).length}
            fallback={<div class="checklist-empty t-hint">{props.emptyText ?? 'nothing to list'}</div>}
          >
            {/*
              * INDEX, NOT For. `For` keys by object identity, and a caller that
              * builds its options inline -- which is the natural way to write
              * them -- hands over a brand new array every render. Every row was
              * then destroyed and rebuilt whenever anything changed, so a click
              * landed on a node that no longer existed and the list flickered
              * under the pointer. These rows are positional: a channel is its
              * index, and Index says so.
              */}
            <Index each={props.options}>
              {(o) => (
                <div class="checklist-row" classList={{ off: !!o().disabled }}>
                  <Toggle
                    label={o().name}
                    value={isOn(o().id)}
                    disabled={!!o().disabled}
                    onChange={(v) => toggle(o().id, v)}
                  />
                  <Show when={o().hint}>
                    <span class="checklist-hint t-hint">{o().hint}</span>
                  </Show>
                </div>
              )}
            </Index>
          </Show>
        </div>
      </Show>
    </div>
  );
}
