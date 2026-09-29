/*
 * The bus name field. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * LOCAL TO THIS EDITOR for the reason Meter.jsx states: the kit keeps only what
 * a second caller has asked for.
 *
 * WHY IT COMMITS ON BLUR AND ENTER RATHER THAN ON EVERY KEYSTROKE. The name
 * goes into shared memory under a seqlock, and every commit is a write other
 * processes may be reading. Sending one per character would be sixteen writes
 * to type "Bass" -- harmless, but it also means every receiver's dropdown
 * flickers through "B", "Ba", "Bas" while you type. A name is finished when you
 * stop typing it.
 */
import { createSignal } from 'solid-js';

/* The plugin clamps to 31 bytes and drops control characters and colons
 * (listenin::wire::parse_label). maxlength here is a courtesy so the field
 * cannot show more than will survive -- it is NOT the enforcement, because a
 * paste of multi-byte characters is longer in bytes than in characters and
 * only the C++ knows that. */
const MAXLEN = 31;

export function NameField(props) {
  const [draft, setDraft] = createSignal(null);

  const shown = () => (draft() === null ? (props.value ?? '') : draft());

  const commit = () => {
    const v = draft();
    setDraft(null);
    if (v !== null && v !== props.value) props.onCommit?.(v);
  };

  return (
    <input
      class="name-field"
      type="text"
      maxlength={MAXLEN}
      placeholder="name this bus"
      aria-label="Bus name"
      value={shown()}
      onInput={(e) => setDraft(e.currentTarget.value)}
      onBlur={commit}
      onKeyDown={(e) => {
        if (e.key === 'Enter') { e.currentTarget.blur(); }
        /* Escape abandons the edit rather than committing it. */
        if (e.key === 'Escape') { setDraft(null); e.currentTarget.blur(); }
        /* The editor is inside a host that may treat keys as its own. */
        e.stopPropagation();
      }}
    />
  );
}

export default NameField;
