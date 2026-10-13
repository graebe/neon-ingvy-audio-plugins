// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A Schwung `chain_params` entry, as the editor needs it. Pure.
 *
 * On the Move a parameter is a KEY with a WIRE value -- a number as text, an
 * enum's index or its label, as the entry's `wire_format` says -- and the
 * module declares its range, its options, its default and its unit in
 * chain_params (engines/<p>/crates/<p>-move/src/params.rs). The editors speak
 * normalised 0..1 and show text (lib/host.js). This is the conversion, from the
 * declaration alone, so the page invents no range of its own.
 *
 *   paramOf(entry) -> {
 *     key, kind,                 'float' | 'enum'
 *     norm(wire) -> 0..1         undefined for a wire value that is not one
 *     wire(norm) -> string       what setParam sends
 *     text(wire) -> string       the display text
 *     parse(text) -> string      typed text -> wire; undefined if it is none
 *     defaultNorm                the declared default, normalised
 *   }
 *
 * DISPLAY TEXT FOLLOWS THE DECLARATION. A float shows as many decimals as its
 * `step` has; a `%` unit over 0..1 is a fraction shown as a percentage (Depth's
 * 1.00 reads "100 %"), and over any other range is a percentage already
 * (Attack's 35.0 reads "35.0 %"). An enum shows its option.
 */

const clamp01 = (n) => Math.min(1, Math.max(0, n));

/* Decimals in a step: 0.01 -> 2, 0.1 -> 1, 1 -> 0. */
export function decimalsOf(step) {
  if (!(step > 0) || step >= 1) return 0;
  return Math.min(6, Math.max(0, Math.ceil(-Math.log10(step) - 1e-9)));
}

function floatParam(e) {
  const min = Number(e.min ?? 0);
  const max = Number(e.max ?? 1);
  const span = max - min;
  const fraction = e.unit === '%' && min >= 0 && max <= 1;
  const shown = fraction ? Math.max(0, decimalsOf(Number(e.step)) - 2) : decimalsOf(Number(e.step));
  const unit = e.unit ? ` ${e.unit}` : '';
  const valueOf = (wire) => {
    const v = Number.parseFloat(wire);
    return Number.isFinite(v) ? v : undefined;
  };
  const p = {
    key: e.key,
    kind: 'float',
    norm(wire) {
      const v = valueOf(wire);
      if (v === undefined) return undefined;
      return span === 0 ? 0 : clamp01((v - min) / span);
    },
    wire(n) {
      const v = min + span * clamp01(Number(n));
      return String(Number(v.toFixed(Math.max(decimalsOf(Number(e.step)), 2) + 2)));
    },
    text(wire) {
      const v = valueOf(wire);
      if (v === undefined) return '';
      return `${(fraction ? v * 100 : v).toFixed(shown)}${unit}`;
    },
    parse(text) {
      const v = Number.parseFloat(String(text).replace(',', '.'));
      if (!Number.isFinite(v)) return undefined;
      const real = fraction ? v / 100 : v;
      return p.wire(span === 0 ? 0 : (real - min) / span);
    },
  };
  p.defaultNorm = p.norm(e.default);
  return p;
}

function enumParam(e) {
  const options = (e.options ?? []).map(String);
  const last = Math.max(1, options.length - 1);
  const byIndex = e.wire_format === 'index';
  /* An index or a label, whichever the wire carried: a module answers in its
   * own convention, and a label is accepted on an index wire too. */
  const indexOf = (wire) => {
    const s = String(wire ?? '').trim();
    const at = options.indexOf(s);
    if (byIndex && /^\d+$/.test(s)) return Number(s) < options.length ? Number(s) : undefined;
    if (at >= 0) return at;
    if (/^\d+$/.test(s) && Number(s) < options.length) return Number(s);
    return undefined;
  };
  const p = {
    key: e.key,
    kind: 'enum',
    options,
    norm(wire) {
      const i = indexOf(wire);
      return i === undefined ? undefined : i / last;
    },
    wire(n) {
      const i = Math.round(clamp01(Number(n)) * last);
      return byIndex ? String(i) : options[i] ?? '';
    },
    text(wire) {
      const i = indexOf(wire);
      return i === undefined ? '' : options[i];
    },
    parse(text) {
      const s = String(text).trim().toLowerCase();
      const i = options.findIndex((o) => o.toLowerCase() === s);
      return i < 0 ? undefined : p.wire(i / last);
    },
  };
  p.defaultNorm = p.norm(e.default);
  return p;
}

/** The conversion for one entry, or null for one that is not a value (a
 * canvas, a read-only string). */
export function paramOf(entry) {
  if (!entry || typeof entry.key !== 'string') return null;
  if (entry.type === 'float' || entry.type === 'int') return floatParam(entry);
  if (entry.type === 'enum') return enumParam(entry);
  return null;
}
