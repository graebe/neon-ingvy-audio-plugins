/*
 * The generated device, structurally. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * WHAT THIS CAN AND CANNOT SAY. It cannot say the device LOOKS right, or
 * that Max opens it -- only Max can say either, and the m4l.md checklist is
 * where that lives. What it can say is that the file is the shape a .amxd
 * is, and that the two facts the generator exists to protect are still true:
 *
 *   EVERY PARAMETER'S `num` INDEX IS ITS tg_param_t INDEX. A live.dial wired
 *   to the wrong index produces a device that works and means something
 *   else, which is the failure TranceGate.h refuses to carry a mapping table
 *   to avoid.
 *
 *   THE RATE MENU IS THE ENGINE'S LADDER, in the engine's order. rates.rs
 *   says "APPENDED, never inserted: RATE_DEFAULT is an index into this
 *   table" -- a reordered menu silently re-points every saved patch.
 *
 * The container framing is checked against Live's own template rather than
 * against this test's belief about it, when that template can be found.
 *
 *   node --test ui/test/device.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const LIVE = join(here, '..', '..');
const ROOT = join(LIVE, '..', '..');
const DEVICE = join(LIVE, 'NI Trance Gate Live.amxd');
const RATES = join(ROOT, 'engines/trance-gate/crates/tg-core/src/rates.rs');

/** The three length-prefixed chunks a .amxd is made of. */
function chunks(buf) {
  const out = {};
  let off = 0;
  while (off + 8 <= buf.length) {
    const tag = buf.toString('ascii', off, off + 4);
    const n = buf.readUInt32LE(off + 4);
    out[tag] = buf.subarray(off + 8, off + 8 + n);
    off += 8 + n;
  }
  return out;
}

const raw = readFileSync(DEVICE);
const c = chunks(raw);
const patcher = JSON.parse(c.ptch.toString('utf8').replace(/\0+$/, '')).patcher;
const boxes = patcher.boxes.map((b) => b.box);

test('the container is the three chunks Live writes, and they span the file', () => {
  assert.ok(c.ampf, 'no ampf chunk');
  assert.ok(c.meta, 'no meta chunk');
  assert.ok(c.ptch, 'no ptch chunk');
  const total = Object.values(c).reduce((n, d) => n + d.length + 8, 0);
  assert.equal(total, raw.length, 'the chunks do not account for the whole file');
});

test('the JSON is NUL-terminated inside its chunk, as Max writes it', () => {
  assert.equal(c.ptch[c.ptch.length - 1], 0);
});

test('the device is the height Live gives it', () => {
  assert.deepEqual(patcher.openrect, [0, 0, 0, 169]);
  assert.equal(patcher.openinpresentation, 1,
    'a device that opens in patching mode shows the user a pile of wires');
});

/*
 * THE CENTRAL ASSERTION. Walk every parameter control, follow its patchline
 * to the `prepend num <i>` it feeds, and check that <i> is that parameter's
 * position in tg_param_t order.
 */
const ABI = ['Slot', 'Length', 'Rate', 'Join Neighbors', 'Env Time',
             'Env Curve', 'Amount', 'Width', 'Attack', 'Decay',
             'Sustain', 'Release', 'Fade', 'Fade Soft'];

test('every control sends the tg_param_t index it belongs to', () => {
  const byId = Object.fromEntries(boxes.map((b) => [b.id, b]));
  const params = boxes.filter((b) => b.parameter_enable);
  /* Read from the engine, not written down: TG_P_COUNT has grown once
   * already and a device quietly two parameters short is what that cost. */
  assert.equal(params.length, ABI.length,
    `the device has ${params.length} parameters, tg_param_t has ${ABI.length}`);

  for (const p of params) {
    const name = p.saved_attribute_attributes.valueof.parameter_longname;
    const want = ABI.indexOf(name);
    assert.ok(want >= 0, `"${name}" is not one of the fourteen`);

    const line = patcher.lines
      .map((l) => l.patchline)
      .find((l) => l.source[0] === p.id && l.source[1] === 0);
    assert.ok(line, `${name} is not connected to anything`);

    const target = byId[line.destination[0]];
    assert.match(target.text ?? '', /^prepend num \d+$/,
      `${name} feeds "${target.text}" rather than a prepend`);
    const got = Number(target.text.split(' ')[2]);
    assert.equal(got, want,
      `${name} sends index ${got}; tg_param_t says ${want}`);
  }
});

test('the rate menu is the engine ladder, in the engine order', () => {
  const src = readFileSync(RATES, 'utf8');
  const rates = [...src.matchAll(/Rate\s*\{\s*label:\s*"([^"]+)"/g)].map((m) => m[1]);

  const menu = boxes.find((b) => b.parameter_enable
    && b.saved_attribute_attributes.valueof.parameter_longname === 'Rate');
  assert.deepEqual(menu.saved_attribute_attributes.valueof.parameter_range, rates);
  /* RATE_DEFAULT, which is an index into exactly that list. */
  const def = Number(src.match(/RATE_DEFAULT:\s*usize\s*=\s*(\d+)/)[1]);
  assert.deepEqual(menu.saved_attribute_attributes.valueof.parameter_initial, [def]);
});

test('audio runs plugin~ -> ni.trancegate~ -> plugout~ on both channels', () => {
  const find = (t) => boxes.find((b) => (b.text ?? '').startsWith(t))?.id;
  const [pin, gate, pout] = ['plugin~', 'ni.trancegate~', 'plugout~'].map(find);
  assert.ok(pin && gate && pout, 'an audio object is missing');
  const has = (s, so, d, di) => patcher.lines.some((l) =>
    l.patchline.source[0] === s && l.patchline.source[1] === so
    && l.patchline.destination[0] === d && l.patchline.destination[1] === di);
  for (const ch of [0, 1]) {
    assert.ok(has(pin, ch, gate, ch), `plugin~ channel ${ch} is not connected`);
    assert.ok(has(gate, ch, pout, ch), `plugout~ channel ${ch} is not connected`);
  }
});

test('the grid is in presentation and the readout reaches it', () => {
  const grid = boxes.find((b) => b.maxclass === 'v8ui');
  assert.ok(grid, 'there is no v8ui');
  assert.equal(grid.presentation, 1, 'the grid must be in the device row');
  const gate = boxes.find((b) => (b.text ?? '').startsWith('ni.trancegate~')).id;
  /* Outlet 2 is the readout -- 0 and 1 are signals. */
  assert.ok(patcher.lines.some((l) =>
    l.patchline.source[0] === gate && l.patchline.source[1] === 2
    && l.patchline.destination[0] === grid.id),
    'the readout does not reach the grid');
  assert.ok(patcher.lines.some((l) =>
    l.patchline.source[0] === grid.id
    && l.patchline.destination[0] === gate),
    'the grid cannot send an edit back');
});

test('every parameter control is in the presentation layer', () => {
  for (const p of boxes.filter((b) => b.parameter_enable)) {
    const name = p.saved_attribute_attributes.valueof.parameter_longname;
    assert.equal(p.presentation, 1, `${name} would be invisible in the chain`);
    const [x, y, w, h] = p.presentation_rect;
    assert.ok(y + h <= 169, `${name} falls outside the 169px device row`);
    assert.ok(x >= 0 && w > 0, `${name} has a nonsense rect`);
  }
});

/* The framing, checked against Live's own template when it is installed. */
const TEMPLATE = '/Applications/Ableton Live 12 Suite.app/Contents/'
  + 'App-Resources/Misc/Max Devices/Max Audio Effect.amxd';

test("the framing matches Live's own blank device",
  { skip: !existsSync(TEMPLATE) }, () => {
    const t = chunks(readFileSync(TEMPLATE));
    assert.deepEqual([...c.ampf], [...t.ampf], 'ampf payload differs');
    assert.equal(c.meta.length, t.meta.length, 'meta length differs');
  });
