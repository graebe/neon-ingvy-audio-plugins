/*
 * The Ultraviolet palette, for mgraphics. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * WHY THIS FILE IS ALLOWED TO SPELL COLOURS when nothing else outside
 * tokens.css is. Max's mgraphics takes RGBA floats and cannot read a
 * stylesheet, so the M4L grid cannot say `var(--uv)` the way every other
 * surface in this repository does. The choice is between a literal at each
 * of the twenty-odd places the grid sets a source, and ONE file that holds
 * them -- which is the same choice tokens.css already made, for the same
 * reason, in the language it had.
 *
 * IT IS NOT A SECOND SOURCE OF TRUTH. ui/test/tokens.test.mjs parses
 * ui-kit/src/tokens.css and asserts that every value below still equals the
 * one it is named after. A token edited in the stylesheet and not here fails
 * there rather than shipping a device that is a slightly different violet
 * from the plugin -- which is precisely the failure the guard was written
 * for: "an arc glow at alpha 0.75 against the system's 0.45, which no eye was
 * ever going to catch".
 *
 * So: add a colour to tokens.css first, then mirror it here, and the test
 * will tell you if you got it wrong.
 */

/** #rrggbb -> mgraphics' [r, g, b, a], each 0..1. */
const rgb = (hex, a = 1) => [
  parseInt(hex.slice(1, 3), 16) / 255,
  parseInt(hex.slice(3, 5), 16) / 255,
  parseInt(hex.slice(5, 7), 16) / 255,
  a,
];

/*
 * Each entry names the tokens.css custom property it mirrors. The test reads
 * these names, so a typo here is a failure rather than a silent skip.
 */
export const TOKEN_SOURCE = {
  bg000:   '--bg-000',
  bg200:   '--bg-200',
  bg300:   '--bg-300',
  line100: '--line-100',
  line200: '--line-200',
  ink:     '--ink',
  inkDim:  '--ink-dim',
  uv:      '--uv',
  uvDeep:  '--uv-deep',
  onUv:    '--on-uv',
};

export const T = {
  bg000:   rgb('#060410'),
  bg200:   rgb('#140e24'),
  bg300:   rgb('#1d1533'),
  line100: rgb('#1d1533'),
  line200: rgb('#3a2a66'),
  ink:     rgb('#f3ecff'),
  inkDim:  rgb('#7a5fb5'),
  uv:      rgb('#efe3ff'),
  uvDeep:  rgb('#a259ff'),
  onUv:    rgb('#060410'),
};

/*
 * THE PLAYHEAD CROSSING A PAD THAT IS ALREADY LIT. on-uv at a quarter alpha,
 * so it reads as a shadow moving across the row -- NOT uv-deep, which the
 * system says is never a fill. tokens.css states this rule in prose next to
 * --on-uv; this is the same decision, applied.
 */
export const PAD_DIP = rgb('#060410', 0.25);

/* The halo around a lit element: uv-deep at half alpha, matching --uv-glow. */
export const UV_GLOW = rgb('#a259ff', 0.5);
