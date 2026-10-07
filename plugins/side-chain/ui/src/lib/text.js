// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The words the Side-Chain window writes: its note names and its hint clauses.
 */

/* Live's octave numbering, where 36 is C1 -- the same table Params.cpp
 * generates for the host's own menu. */
const PITCH = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
export const NOTE_NAMES = Array.from({ length: 128 },
  (_, i) => `${PITCH[i % 12]}${Math.floor(i / 12) - 2}`);

/*
 * THE HINT BAR STATES THIS WINDOW'S CONVENTIONS ONCE, three short clauses at
 * most: the verb, then the rest. The third is the one thing a reader cannot
 * work out by looking -- what the times are in milliseconds right now, or that
 * they are percentages of the cycle.
 */
export function hintFor(source, ms, timeModeNorm) {
  const showMs = (timeModeNorm ?? 0) < 0.5;
  const total = ms.reduce((a, b) => a + b, 0);
  const clauses = [['drag', 'a handle, shift for fine']];
  if (source === 1) clauses.push(['midi', 'needs a MIDI track']);
  else if (source === 2) clauses.push(['key', 'route it in the header']);
  else clauses.push(['cycle', 'follows the transport']);
  clauses.push(['times', showMs ? `${Math.round(total)} ms total` : '% of the cycle']);
  return clauses;
}
