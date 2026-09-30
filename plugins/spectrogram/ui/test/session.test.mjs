/*
 * The session state: decoded from the table the plugin's own encoder writes
 * (tests/cpp/spectro_wire.cpp, the "state" lines of wire_table.txt), and
 * applied before the editor is allowed to push anything.
 * Copyright (c) 2026 Torben Gräber. MIT.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { decodeState, rangeIndex, createPushGate } from '../src/lib/session.js';
import { RANGES } from '../src/lib/columns.js';

const here = dirname(fileURLToPath(import.meta.url));
const TABLE = process.env.SPECTRO_WIRE_TABLE ?? join(here, 'wire_table.txt');
const CASES = readFileSync(TABLE, 'utf8').split('\n')
  .filter((l) => l.startsWith('state '))
  .map((l) => {
    const [, lo, hi, view, a, b, on, floor, bal, encoded] = l.split(' ');
    return {
      want: {
        lo: Number(lo), hi: Number(hi),
        view: view === '-' ? [0] : view.split(',').map(Number),
        cmpA: Number(a), cmpB: Number(b), clashOn: on === '1',
        floorDb: Number(floor), balanceDb: Number(bal),
      },
      encoded,
    };
  });

test('the table has state cases', () => {
  assert.ok(CASES.length >= 4);
});

test('every state the plugin encodes decodes to what it was given', () => {
  for (const c of CASES) assert.deepEqual(decodeState(c.encoded), c.want, c.encoded);
});

test('a malformed state is refused whole', () => {
  assert.equal(decodeState('10:20000:0:0:1:0:-60'), null);          /* a field short */
  assert.equal(decodeState('10:20000::0:1:0:-60:12'), null);        /* no view */
  assert.equal(decodeState('10:20000:0:0:1:2:-60:12'), null);       /* not a flag */
  assert.equal(decodeState('800:40:0:0:1:0:-60:12'), null);         /* inverted range */
  assert.equal(decodeState(undefined), null);
});

test('a range in Hz is the named zoom it matches, or Full', () => {
  assert.equal(rangeIndex(40, 800, RANGES), 2);
  assert.equal(rangeIndex(2000, 20000, RANGES), 4);
  assert.equal(rangeIndex(123, 456, RANGES), 0);
});

/*
 * THE BUG: the editor's first push replaced the saved session with its
 * defaults. Nothing may be sent until the plugin's state has been applied.
 */
test('nothing is pushed before the session has been restored', () => {
  const sent = [];
  const gate = createPushGate();
  const push = () => gate.send(() => sent.push('view'));
  assert.equal(push(), false);
  assert.deepEqual(sent, []);
  gate.open();
  assert.equal(push(), true);
  assert.deepEqual(sent, ['view']);
});
