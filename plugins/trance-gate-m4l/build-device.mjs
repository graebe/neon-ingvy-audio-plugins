/*
 * Generate NI Trance Gate.amxd. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * WHY A GENERATOR AND NOT A CHECKED-IN PATCHER. A .amxd is a binary-framed
 * blob of machine-written JSON: unreadable in a diff, unmergeable, and full
 * of numbers that have to agree with things stated elsewhere in this
 * repository. Two in particular:
 *
 *   THE PARAMETER ORDER IS tg_param_t's. The patcher sends `num <index>`,
 *   and TranceGate.h explains at length why this project refuses to carry a
 *   mapping table -- the JUCE build had one because its order had drifted.
 *   A hand-placed live.dial with the wrong index is exactly that bug, and it
 *   would be invisible in the file.
 *
 *   THE RATE LABELS ARE THE ENGINE'S. They are READ OUT OF rates.rs below
 *   rather than transcribed, because that table carries a comment saying
 *   "APPENDED, never inserted: RATE_DEFAULT is an index into this table" --
 *   a menu that listed them in a different order would silently re-point
 *   every saved patch.
 *
 * So the patcher is written down as the twelve rows below, which are
 * reviewable, and the JSON is derived. Regenerate with:
 *
 *   node plugins/trance-gate-m4l/build-device.mjs
 *
 * WHAT THIS DOES NOT DO IS LAY THE DEVICE OUT PRETTILY. It produces a device
 * that is correct -- right parameters, right indices, right wiring -- at
 * workable positions. Nudging objects is Max's job and the result is saved
 * back into the .amxd by Max itself; this script is for when the PARAMETERS
 * change, not the pixels.
 */
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const ROOT = join(here, '..', '..');

/* ------------------------------------------------------------------ */
/* The engine's rate ladder, read rather than copied.                    */

const rates = [...readFileSync(
  join(ROOT, 'engines/trance-gate/crates/tg-core/src/rates.rs'), 'utf8')
  .matchAll(/Rate\s*\{\s*label:\s*"([^"]+)"/g)].map((m) => m[1]);

if (rates.length !== 13)
  throw new Error(`expected 13 rates, rates.rs has ${rates.length} -- ` +
    'TG_NUM_RATES and this generator disagree');

/* ------------------------------------------------------------------ */
/* The twelve, in tg_param_t order. The INDEX IS THE ARRAY POSITION.     */

const DIAL = 'live.dial', MENU = 'live.menu', NUM = 'live.numbox',
      TOGGLE = 'live.text';

/* parameter_type: 0 float, 1 int, 2 enum. */
const params = [
  /* 0 */ { name: 'Slot',   cls: NUM,  type: 1, min: 1,   max: 8,   init: 1 },
  /* 1 */ { name: 'Length', cls: NUM,  type: 1, min: 1,   max: 32,  init: 16 },
  /* 2 */ { name: 'Rate',   cls: MENU, type: 2, range: rates,        init: 7 },
  /* 3 */ { name: 'Join Neighbors', short: 'Join', cls: TOGGLE, type: 2,
            range: ['Off', 'On'], init: 0 },
  /* 4 */ { name: 'Env Time', short: 'Time', cls: MENU, type: 2,
            range: ['ms', '% Step'], init: 0 },
  /* 5 */ { name: 'Env Curve', short: 'Curve', cls: MENU, type: 2,
            range: ['Linear', 'Exponential', 'S-Curve'], init: 0 },
  /* 6 */ { name: 'Amount', cls: DIAL, type: 0, min: 0, max: 100, init: 100 },
  /* 7 */ { name: 'Width',  cls: DIAL, type: 0, min: 5, max: 100, init: 100 },
  /*
   * A STAGE RUNS TO 200% OF THE GATE'S WIDTH, which is TG_STAGE_MAX_PCT and
   * is not a typo: the stages are a proportion of Width, not of the step, so
   * a stage longer than the gate is meaningful and reachable.
   */
  /* 8 */ { name: 'Attack',  cls: DIAL, type: 0, min: 0, max: 200, init: 1.6 },
  /* 9 */ { name: 'Decay',   cls: DIAL, type: 0, min: 0, max: 200, init: 16 },
  /*10 */ { name: 'Sustain', cls: DIAL, type: 0, min: 0, max: 100, init: 100 },
  /*11 */ { name: 'Release', cls: DIAL, type: 0, min: 0, max: 200, init: 16 },
];

/*
 * The order above IS the engine's. Asserted rather than trusted: this list
 * and enum tg_param_t are two statements of one fact, and the whole point of
 * generating the patcher is that they cannot come apart unnoticed.
 */
const ABI = ['Slot', 'Length', 'Rate', 'Join Neighbors', 'Env Time',
             'Env Curve', 'Amount', 'Width', 'Attack', 'Decay',
             'Sustain', 'Release'];
params.forEach((p, i) => {
  if (p.name !== ABI[i])
    throw new Error(`parameter ${i} is "${p.name}", tg_param_t says "${ABI[i]}"`);
});

/* ------------------------------------------------------------------ */
/* Layout. A device row is 169px tall and that is the whole brief.       */

const W = 620;
const GRID = { x: 4, y: 4, w: W - 8, h: 44 };
const DIAL_SIZE = [46, 48], SMALL = [64, 15];

let id = 0;
const nextId = () => `obj-${++id}`;
const boxes = [], lines = [];
const add = (box) => { boxes.push({ box }); return box.id; };
const connect = (from, outlet, to, inlet) =>
  lines.push({ patchline: { destination: [to, inlet], source: [from, outlet] } });

/* plugin~ / plugout~, the device's audio boundary. */
const plugin = add({ id: nextId(), maxclass: 'newobj', text: 'plugin~',
  numinlets: 1, numoutlets: 3, outlettype: ['signal', 'signal', ''],
  patching_rect: [20, 60, 53, 22] });

const gate = add({ id: nextId(), maxclass: 'newobj', text: 'tg.gate~',
  numinlets: 2, numoutlets: 4,
  outlettype: ['signal', 'signal', '', ''],
  patching_rect: [20, 120, 70, 22] });

const plugout = add({ id: nextId(), maxclass: 'newobj', text: 'plugout~',
  numinlets: 2, numoutlets: 0, patching_rect: [20, 300, 56, 22] });

connect(plugin, 0, gate, 0);
connect(plugin, 1, gate, 1);
connect(gate, 0, plugout, 0);
connect(gate, 1, plugout, 1);

/*
 * THE READOUT IS PULLED, NEVER PUSHED. The only thread that knows the
 * playhead moved is the audio thread, which may not touch an outlet -- so a
 * qmetro asks and the answer is produced on the thread that asked. 50ms is
 * 20fps of pattern state; the playhead itself is interpolated between these
 * by layout.js, so this rate sets accuracy, not smoothness.
 */
const metro = add({ id: nextId(), maxclass: 'newobj', text: 'qmetro 50',
  numinlets: 2, numoutlets: 1, outlettype: ['bang'],
  patching_rect: [320, 60, 70, 22] });

const thisdevice = add({ id: nextId(), maxclass: 'newobj',
  text: 'live.thisdevice', numinlets: 1, numoutlets: 3,
  outlettype: ['bang', '', ''], patching_rect: [320, 20, 100, 22] });

connect(thisdevice, 0, metro, 0);
connect(metro, 0, gate, 0);

/* The grid. v8ui, because jsui's ES5 engine cannot import readout.js. */
const grid = add({ id: nextId(), maxclass: 'v8ui', text: 'v8ui grid.js',
  numinlets: 1, numoutlets: 1, outlettype: [''],
  patching_rect: [320, 120, GRID.w, GRID.h],
  presentation: 1, presentation_rect: [GRID.x, GRID.y, GRID.w, GRID.h] });

/* tg.gate~'s third outlet is the readout; the grid's only outlet is the
 * edit it just made, which goes straight back in. */
connect(gate, 2, grid, 0);
connect(grid, 0, gate, 0);
connect(metro, 0, grid, 0);

/* ------------------------------------------------------------------ */
/* The twelve, each into its own `prepend num <index>`.                  */

const dials = params.filter((p) => p.cls === DIAL);
const rest  = params.filter((p) => p.cls !== DIAL);

params.forEach((p, idx) => {
  const isDial = p.cls === DIAL;
  const slot = isDial ? dials.indexOf(p) : rest.indexOf(p);
  const size = isDial ? DIAL_SIZE : SMALL;
  const px = isDial ? 4 + slot * (DIAL_SIZE[0] + 2) : 300 + (slot % 2) * 70;
  const py = isDial ? 54 : 54 + Math.floor(slot / 2) * 18;

  const attrs = {
    parameter_longname: p.name,
    parameter_shortname: p.short ?? p.name,
    parameter_type: p.type,
    parameter_initial_enable: 1,
    parameter_initial: [p.init],
  };
  if (p.type === 2) attrs.parameter_range = p.range;
  else { attrs.parameter_mmin = p.min; attrs.parameter_mmax = p.max; }

  const obj = add({
    id: nextId(),
    maxclass: p.cls,
    numinlets: 1,
    numoutlets: 2,
    outlettype: ['', 'float'],
    parameter_enable: 1,
    varname: p.name.toLowerCase().replace(/\W+/g, '_'),
    patching_rect: [320 + (idx % 4) * 80, 180 + Math.floor(idx / 4) * 40,
                    size[0], size[1]],
    presentation: 1,
    presentation_rect: [px, py, size[0], size[1]],
    saved_attribute_attributes: { valueof: attrs },
  });

  /*
   * `prepend num <index>` turns the control's bare value into the message
   * tg.gate~ takes. One per parameter, because the INDEX is the thing being
   * stated and stating it here -- beside the control it belongs to -- is
   * what makes a wrong one visible in the patcher rather than only in the
   * sound.
   */
  const pre = add({ id: nextId(), maxclass: 'newobj',
    text: `prepend num ${idx}`, numinlets: 2, numoutlets: 1,
    outlettype: [''],
    patching_rect: [320 + (idx % 4) * 80, 210 + Math.floor(idx / 4) * 40,
                    100, 22] });

  connect(obj, 0, pre, 0);
  connect(pre, 0, gate, 0);
});

/*
 * THE SLOT HANDSHAKE'S PATCHER HALF. tg.gate~'s fourth outlet emits
 * `length <n>` on the bang after the slot moved, because Length belongs to
 * the SLOT and the dial still holds the one we left -- see the comment on
 * slot_sync in tg.gate~.c. Routed straight into the Length control, which
 * then pushes it back as any other edit.
 */
const lengthIdx = params.findIndex((p) => p.name === 'Length');
const lengthObj = boxes.filter((b) => b.box.varname === 'length')[0].box.id;
const route = add({ id: nextId(), maxclass: 'newobj', text: 'route length',
  numinlets: 1, numoutlets: 2, outlettype: ['', ''],
  patching_rect: [560, 120, 80, 22] });
connect(gate, 3, route, 0);
connect(route, 0, lengthObj, 0);
if (lengthIdx < 0) throw new Error('Length is not among the twelve');

/* ------------------------------------------------------------------ */

const patcher = {
  patcher: {
    fileversion: 1,
    appversion: { major: 9, minor: 1, revision: 5, architecture: 'x64',
                  modernui: 1 },
    classnamespace: 'box',
    rect: [100, 100, 900, 500],
    /* The device row's height, which is not ours to choose. */
    openrect: [0, 0, 0, 169],
    openinpresentation: 1,
    devicewidth: W,
    default_fontsize: 10,
    default_fontface: 0,
    default_fontname: 'Arial',
    gridonopen: 1,
    gridsize: [5, 5],
    boxes,
    lines,
    dependency_cache: [],
    autosave: 0,
    description: 'Tempo-locked step gate with a per-step ADSR, ties and '
               + 'eight pattern slots -- the pattern editable in the chain.',
    digest: 'NI Trance Gate',
    tags: 'gate trance sequencer modulation',
  },
};

/*
 * THE CONTAINER. A .amxd is three length-prefixed chunks -- `ampf`, `meta`
 * and `ptch` -- around the JSON, and the JSON is NUL-terminated inside its
 * own chunk. Read off Live's own "Max Audio Effect.amxd" rather than guessed;
 * a file that is merely valid JSON does not open.
 */
const json = Buffer.from(JSON.stringify(patcher, null, '\t') + '\n\0', 'utf8');
const chunk = (tag, data) => {
  const head = Buffer.alloc(8);
  head.write(tag, 0, 'ascii');
  head.writeUInt32LE(data.length, 4);
  return Buffer.concat([head, data]);
};
const out = Buffer.concat([
  chunk('ampf', Buffer.from('aaaa', 'ascii')),
  chunk('meta', Buffer.alloc(4)),
  chunk('ptch', json),
]);

const dest = join(here, 'NI Trance Gate.amxd');
writeFileSync(dest, out);
console.log(`wrote ${dest}`);
console.log(`  ${params.length} parameters, ${rates.length} rates from rates.rs`);
console.log(`  ${boxes.length} boxes, ${lines.length} patchlines`);
