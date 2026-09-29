/*
 * The intensity ramp: one byte of level to one colour.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * NO COLOUR IS SPELLED HERE, and that is the point of the file existing at all.
 * The five stops live in uv.css as --spec-0..--spec-4 and are read back through
 * getComputedStyle, so a canvas -- which cannot use a CSS variable directly --
 * still gets its colours from the one place the design system is transcribed.
 *
 * The alternative was an array of five hex strings in the drawing code, which is
 * exactly the drift the token guard exists to catch: next door, an arc glow sat
 * inline at alpha 0.75 against the system's 0.45 for as long as it existed, and
 * nobody was ever going to see it.
 *
 * INTERPOLATION IS IN sRGB, not in a perceptual space. The stops were chosen by
 * eye against the picture they produce, so interpolating them somewhere else
 * would change the thing that was judged.
 */

export const STOPS = 5;
export const LEVELS = 256;

const parseHex = (text) => {
  const hex = String(text).trim().replace(/^#/, '');
  if (!/^[0-9a-fA-F]{6}$/.test(hex)) return null;
  return [
    parseInt(hex.slice(0, 2), 16),
    parseInt(hex.slice(2, 4), 16),
    parseInt(hex.slice(4, 6), 16),
  ];
};

/** The five stops, read from the document's own custom properties. */
export function readStops(el = document.documentElement) {
  const style = getComputedStyle(el);
  const stops = [];
  for (let i = 0; i < STOPS; i++) {
    const rgb = parseHex(style.getPropertyValue(`--spec-${i}`));
    /*
     * A MISSING STOP IS A BUG IN THE STYLESHEET, NOT A CASE TO PAPER OVER.
     * Falling back to something plausible would leave the picture looking
     * nearly right with a ramp nobody chose -- so it throws, and the harness
     * and the ctest both see it immediately.
     */
    if (!rgb) throw new Error(`--spec-${i} is not a 6-digit hex colour`);
    stops.push(rgb);
  }
  return stops;
}

/**
 * A 256-entry lookup table, flat RGB triples: `lut[v * 3 + channel]`.
 *
 * Byte 0 is the floor and maps to --spec-0 EXACTLY, which is why silence is
 * indistinguishable from the well it is drawn in rather than being the first
 * visible step of a gradient.
 */
export function buildLut(stops = readStops()) {
  const lut = new Uint8Array(LEVELS * 3);
  const segments = STOPS - 1;
  for (let v = 0; v < LEVELS; v++) {
    const x = (v / (LEVELS - 1)) * segments;
    /* The last level sits exactly on the final stop, so clamp the segment
     * index rather than letting it index one past the end. */
    const i = Math.min(segments - 1, Math.floor(x));
    const t = x - i;
    for (let c = 0; c < 3; c++) {
      lut[v * 3 + c] = Math.round(stops[i][c] + (stops[i + 1][c] - stops[i][c]) * t);
    }
  }
  return lut;
}

/** Rec. 709 relative luminance of a LUT entry, 0..255. For the ramp's test. */
export const luminance = (lut, v) =>
  0.2126 * lut[v * 3] + 0.7152 * lut[v * 3 + 1] + 0.0722 * lut[v * 3 + 2];

/**
 * One stop as the stylesheet wrote it, for the places a canvas wants a CSS
 * colour string rather than three numbers (fillStyle). Reading it back beats
 * reassembling it: an rgb() built here would also be a colour spelled outside
 * uv.css, which is precisely what the token guard forbids.
 */
export const stopCss = (i, el = document.documentElement) =>
  getComputedStyle(el).getPropertyValue(`--spec-${i}`).trim();
