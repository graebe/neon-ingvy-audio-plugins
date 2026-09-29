/*
 * The engine's `ui` readout, decoded. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * WHY THIS IS ITS OWN FILE NOW. It was eleven lines inside App.jsx's message
 * handler, interleaved with the Solid setters it fed, which was right while
 * this editor was the only thing that read the string. The Max for Live
 * device is the second, and the kit's own index.js states the rule this
 * follows: a component with one caller has no API yet, only a shape -- "they
 * move the day a second editor wants one".
 *
 * The alternative was a second decoder in the v8ui, and this repository has
 * already paid for that experiment once: curves.js existed twice, and the
 * JavaScript copy had the S-curve's first half un-mirrored for as long as it
 * existed. Nothing caught it, because a wrong curve is still a curve. A
 * wrongly decoded readout is still a pattern.
 *
 * THE FORMAT IS THE ENGINE'S, and it is emitted by ui_readout() in
 * tg-core/src/params.rs as nine colon-separated fields:
 *
 *     steps : ties : length : pos : ms_per_step : advancing : cursor
 *           : depths : orders
 *
 * `steps` and `ties` are little-endian hex bitmaps, LAST character is the
 * LOWEST nibble. `depths` and `orders` are two hex digits per step, in step
 * order. Both encodings are the engine's business and this file is where
 * they stop.
 *
 * THE FIELD COUNT HAS GROWN ONCE, when the fade added `orders`, and the
 * guard below moved with it. That is the whole argument for one decoder:
 * the WebView editor and the M4L grid both had to learn the ninth field,
 * and with two copies only one of them would have.
 *
 * It is pinned against the engine rather than against this reading of it --
 * see ui/test/readout.test.mjs, which decodes a fixture that the engine
 * itself printed.
 */

/**
 * A little-endian hex bitmap to `n` booleans.
 *
 * THE STRING IS READ FROM ITS RIGHT-HAND END, because the engine's to_hex
 * writes the most significant nibble first and step 0 is the least
 * significant bit. Reading it forwards gives a pattern that is correct only
 * when the length is a multiple of four and mirrored the rest of the time --
 * which looks like a plausible pattern, just not yours.
 */
export const hexToBits = (hex, n) => {
  const bits = new Array(n).fill(false);
  if (!hex) return bits;
  let bit = 0;
  for (let i = hex.length - 1; i >= 0 && bit < n; i--) {
    const v = parseInt(hex[i], 16);
    if (Number.isNaN(v)) continue;
    for (let k = 0; k < 4 && bit < n; k++, bit++) bits[bit] = ((v >> k) & 1) === 1;
  }
  return bits;
};

/**
 * Decode one `ui` readout. Returns null for a string that is not one.
 *
 * NULL RATHER THAN A PARTIAL OBJECT. A short read means the field count
 * changed, and a decoder that filled the gaps with defaults would draw a
 * confident empty pattern over a real one -- which reads as "the device lost
 * my steps" rather than as a version mismatch. The caller keeps its last good
 * state instead.
 */
export function parseReadout(msg) {
  if (typeof msg !== 'string') return null;
  const f = msg.split(':');
  if (f.length < 9) return null;

  const length = Math.max(1, parseInt(f[2], 10) || 16);

  /* Two hex digits per step, in step order -- unlike the bitmaps above. The
   * engine writes them forwards because they are runs of bytes rather than
   * numbers, and mixing the two up is exactly why this is one function. */
  const depths = [], orders = [];
  for (let i = 0; i < length; i++) {
    depths.push((parseInt((f[7] || '').substr(i * 2, 2), 16) || 0) / 255);
    /* The fade's arrival rank, 1..N, and 0 for a step that is off. NOT
     * scaled the way a depth is: it is an ordinal, not a level. */
    orders.push(parseInt((f[8] || '').substr(i * 2, 2), 16) || 0);
  }

  return {
    steps: hexToBits(f[0], length),
    ties: hexToBits(f[1], length),
    length,
    phase: parseFloat(f[3]) || 0,
    msStep: parseFloat(f[4]) || 0,
    moving: f[5] === '1',
    cursor: parseInt(f[6], 10) || 0,
    depths,
    orders,
  };
}
