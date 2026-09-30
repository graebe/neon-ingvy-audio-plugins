/*
 * A KICK, ON DEMAND -- the only way to review the animated ground without a host.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The real onset comes from the Rust detector on the audio thread
 * (engines/ground), so there is nothing here to detect: this fakes the MESSAGE
 * that detector's onsets turn into, which is the same thing the editor sees in
 * Live.
 *
 * It is what separates the two halves when the background does not move: if a
 * kick fired here draws rings, the field and the canvas are fine and the question
 * is whether the detector is firing; if it draws nothing, the field is.
 *
 *   K        one kick at full strength
 *   J        a weak kick (0.3, the detector's floor)
 *   hold B   a 120 BPM four-on-the-floor
 *
 * KEYBOARD ONLY. A window-wide click used to fire a kick too, which meant every
 * click on a control during a review also rang the ground -- so a review of a
 * knob was a review of a knob plus a background effect nobody asked for. The
 * design's own preview clicks the ground itself, but here the ground is
 * pointer-events: none, so there is nothing to click that is not a control.
 *
 * A MODULE, unlike mock.js, and deliberately: the tag comes from the editor's
 * own src/lib/msg.js, so it cannot drift from the value the editor listens on.
 * mock.js has to stay a classic script (see the note in index.html), and a kick
 * is never needed before the editor has loaded, so nothing is lost by this one
 * being deferred.
 */
import { MSG } from '../../src/lib/msg.js';

const b64 = (s) => btoa(String.fromCharCode(...new TextEncoder().encode(s)));
const kick = (strength = 1) =>
  globalThis.SAMFD?.(MSG.ground, 0, b64(strength.toFixed(3)));

let beat = 0;
addEventListener('keydown', (e) => {
  if (e.repeat) return;
  const k = e.key.toLowerCase();
  if (k === 'k') kick(1);
  else if (k === 'j') kick(0.3);
  else if (k === 'b' && !beat) beat = setInterval(() => kick(1), 500);
});
addEventListener('keyup', (e) => {
  if (e.key.toLowerCase() === 'b') { clearInterval(beat); beat = 0; }
});
console.info('harness: press K for a kick, J for a weak one, hold B for 120 BPM');
