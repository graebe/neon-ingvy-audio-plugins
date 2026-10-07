// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the session is looking at, as the plugin remembers it.
 *
 * THE EDITOR USED TO FORGET IT AND THEN OVERWRITE IT. On ready the plugin sent
 * nothing about the view, the comparison, the clash or the zoom, so a reopened
 * editor started from its own defaults -- and its first push replaced the
 * saved session with them. Now the plugin sends its state first, and nothing
 * leaves this editor until it has been applied. Plain JavaScript, so node can
 * test it against the table the plugin's own encoder writes.
 */

/**
 * "<f_min>:<f_max>:<view>:<a>:<b>:<on>:<floor_db>:<balance_db>" -> an object,
 * or null. Refused whole: half a session applied is a session nobody saved.
 */
export function decodeState(text) {
  if (typeof text !== 'string') return null;
  const f = text.split(':');
  if (f.length !== 8) return null;
  const lo = Number(f[0]);
  const hi = Number(f[1]);
  const view = f[2] === '' ? [] : f[2].split(',').map(Number);
  const cmpA = Number(f[3]);
  const cmpB = Number(f[4]);
  const floorDb = Number(f[6]);
  const balanceDb = Number(f[7]);
  if (!(lo > 0) || !(hi > lo)) return null;
  if (!view.length || !view.every((c) => Number.isInteger(c) && c >= 0)) return null;
  if (!Number.isInteger(cmpA) || cmpA < 0 || !Number.isInteger(cmpB) || cmpB < 0) return null;
  if (f[5] !== '0' && f[5] !== '1') return null;
  if (!Number.isFinite(floorDb) || !Number.isFinite(balanceDb)) return null;
  return { lo, hi, view, cmpA, cmpB, clashOn: f[5] === '1', floorDb, balanceDb };
}

/** The named zoom a range in Hz is, or 0 (Full) for one the list does not have. */
export function rangeIndex(lo, hi, ranges) {
  const i = ranges.findIndex((r) => Math.abs(r.lo - lo) < 0.5 && Math.abs(r.hi - hi) < 0.5);
  return i < 0 ? 0 : i;
}

/**
 * A gate on what the editor sends about the session: closed until the
 * plugin's state has been applied, and then open for good.
 */
export function createPushGate() {
  let open = false;
  return {
    open: () => { open = true; },
    isOpen: () => open,
    /** Runs `fn` if the session has been restored; says whether it did. */
    send: (fn) => {
      if (open) fn();
      return open;
    },
  };
}
