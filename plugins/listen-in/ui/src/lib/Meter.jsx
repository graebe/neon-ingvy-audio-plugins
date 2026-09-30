/*
 * The activity meter. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * LOCAL TO THIS EDITOR, NOT IN THE KIT, and that is the kit's own rule rather
 * than an omission -- ui-kit/src/index.js:
 *
 *   "a component with one caller has no API yet -- only a shape. They move the
 *    day a second editor wants one."
 *
 * The Spectrogram draws a field of light and the Trance Gate draws rings;
 * neither wants a bar. When one does, this moves and gets an API on the way.
 *
 * NO COLOUR IS SPELLED HERE. Every value comes from tokens.css through a var(),
 * and `ctest -R ui_tokens` scans this file along with every other editor's --
 * a literal typed in here fails the build, which is exactly what it is for.
 */

/* The peak is a fraction of the well's width, already in dB terms -- see
 * meterFraction in state.js. The component draws; it decides nothing. */
export function Meter(props) {
  return (
    <div class="meter" role="meter" aria-label="Input level"
         aria-valuemin="0" aria-valuemax="100"
         aria-valuenow={Math.round((props.value ?? 0) * 100)}>
      <div
        class="meter-fill"
        classList={{ 'meter-fill-off': !props.active }}
        style={{ width: `${Math.round((props.value ?? 0) * 100)}%` }}
      />
    </div>
  );
}

export default Meter;
