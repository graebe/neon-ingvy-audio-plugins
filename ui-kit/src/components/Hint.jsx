/*
 * The hint bar. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Byte-identical in both editors before the kit existed, which is as good a
 * signal as any that it belonged here.
 */
import { For, Show } from 'solid-js';
import { Signature } from './Signature.jsx';

/*
 * Three clauses at most. Truncating rather than shrinking the text is
 * deliberate: a fourth clause means the window needs simplifying, not a smaller
 * font.
 *
 * THE SIGNATURE CLOSES THE BAR, which is Ultraviolet 1.0.0's rule: it belongs
 * to the WINDOW ("exactly once, at the right end of the Hint bar"), not to any
 * one editor's layout. Carrying it here is what makes that structural -- an
 * editor cannot forget it, put it elsewhere, or show two.
 */
export function Hint(props) {
  return (
    <div class="hint-bar t-hint">
      {/*
        * THE TIPS ARE WRAPPED SO THEY CAN GIVE WAY, and the Signature cannot.
        *
        * NI Listen-In's window is 360px wide. Three clauses and a ~110px
        * signature do not fit in the 296 that leaves, and unwrapped spans in a
        * flex row have nothing to shrink -- the row simply overflowed the
        * window. Wrapping them gives the row one shrinkable child.
        *
        * The tips truncate rather than the branding, which is the right way
        * round: the signature is fixed and the same in every window, so a
        * reader who loses its end loses nothing, while a clipped tip is at
        * least still a readable tip.
        *
        * A window that actually truncates is a window whose hints are too long
        * -- the ellipsis is the signal, not the fix.
        */}
      <span class="hint-tips">
        <For each={props.clauses.slice(0, 3)}>{(c, i) => (
          <>
            <Show when={i() > 0}><span class="sep">–</span></Show>
            <span class="hint-key">{c[0]}</span>
            <span class="hint-val">{c[1]}</span>
          </>
        )}</For>
      </span>
      <Signature />
    </div>
  );
}
