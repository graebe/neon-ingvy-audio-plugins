/*
 * A button. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * From the design system's own `.uv-btn`: 28px tall (control-h), space-3
 * padding, a bg-200 well on a line-200 border, rising to bg-300 under the
 * pointer.
 *
 * `on` is the system's PRIMARY state -- uv fill, on-uv text, glow-led -- and it
 * is the same state the system gives a pressed button, because "there is no
 * separate selected colour: selected is uv". A latched button that is engaged
 * and a momentary button that is held look alike on purpose.
 *
 * IT TAKES CHILDREN RATHER THAN AN ICON NAME. The two editors had two versions
 * of this: one drew a word, the other drew one of two hard-coded SVG glyphs.
 * Baking a glyph set into the kit would have been the wrong half to keep --
 * the system says "the system uses no icon set: state is shown by light and by
 * words. Do not add icons for play, copy or settings; write the word." Where a
 * caller genuinely needs a mark, it passes one as a child and owns it.
 */
export function Button(props) {
  return (
    <button type="button" class="btn t-button"
            classList={{ on: props.on, icon: props.icon }}
            title={props.title}
            onClick={props.onClick}>
      {props.children}
    </button>
  );
}
