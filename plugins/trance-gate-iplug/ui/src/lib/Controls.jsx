/* Selects, glyph buttons, tabs and the hint bar.
 * Copyright (c) 2026 Torben Gräber. MIT. */
import { For } from 'solid-js';
import { setParam, beginGesture, endGesture } from './iplug.js';

/* A CHOICE, NOT A QUANTITY. Rate, Slot, Env Time and Env Curve are ladders;
 * a host that interpolates towards "Slot 4.5" is offering nothing. */
export function Select(props) {
  const n = () => props.options.length;
  const idx = () => Math.round((props.value ?? 0) * (n() - 1));
  const pick = (i) => {
    beginGesture(props.idx);
    setParam(props.idx, n() > 1 ? i / (n() - 1) : 0);
    endGesture(props.idx);
  };
  return (
    <label class="select">
      <span class="select-label">{props.label}</span>
      <select value={idx()} onChange={(e) => pick(+e.currentTarget.value)}>
        <For each={props.options}>{(o, i) => <option value={i()}>{o}</option>}</For>
      </select>
    </label>
  );
}

export function Toggle(props) {
  const on = () => (props.value ?? 0) > 0.5;
  return (
    <button
      class="toggle" classList={{ on: on() }}
      onClick={() => {
        beginGesture(props.idx);
        setParam(props.idx, on() ? 0 : 1);
        endGesture(props.idx);
      }}
    >{props.label}</button>
  );
}

/* Copy and paste as glyphs: the row was mostly words before, and two icons
 * say the same thing in a fifth of the width. */
export function GlyphButton(props) {
  return (
    <button class="glyph" title={props.title} onClick={props.onClick}>
      {props.glyph === 'copy' ? (
        <svg width="14" height="14" viewBox="0 0 14 14">
          <rect x="4.5" y="1.5" width="8" height="8" fill="none" stroke="currentColor" />
          <rect x="1.5" y="4.5" width="8" height="8" fill="none" stroke="currentColor" />
        </svg>
      ) : (
        <svg width="14" height="14" viewBox="0 0 14 14">
          <rect x="2.5" y="3.5" width="9" height="9" fill="none" stroke="currentColor" />
          <path d="M5 3.5 V1.5 h4 v2" fill="none" stroke="currentColor" />
        </svg>
      )}
    </button>
  );
}

export function Tabs(props) {
  return (
    <div class="tabs">
      <For each={props.tabs}>{(t, i) => (
        <button
          class="tab" classList={{ on: props.active === i() }}
          onClick={() => props.onSelect(i())}
        >{t}</button>
      )}</For>
    </div>
  );
}

/* Three clauses at most. Truncating rather than shrinking the text is
 * deliberate: a fourth clause means the window needs simplifying, not a
 * smaller font. */
export function HintBar(props) {
  return (
    <div class="hint-bar">
      <For each={props.clauses.slice(0, 3)}>{(c, i) => (
        <>
          {i() > 0 && <span class="sep">–</span>}
          <span class="hint-key">{c[0]}</span>
          <span class="hint-val">{c[1]}</span>
        </>
      )}</For>
    </div>
  );
}
