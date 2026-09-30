/*
 * Binary payloads: a header, then bytes read in place.
 * Copyright (c) 2026 Torben Gräber. MIT.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readHeader, intField, bipolar, unipolar, reuse } from '../src/lib/capture.js';

const bytes = (header, data) =>
  Uint8Array.from([...header].map((c) => c.charCodeAt(0)).concat(data));

test('the header is read field by field and the data starts after it', () => {
  /* A 58 (':') in the DATA must not be taken for a separator. */
  const b = bytes('256:2000:7:', [58, 0, 255]);
  const h = readHeader(b, 3);
  assert.deepEqual(h.fields, ['256', '2000', '7']);
  assert.equal(h.offset, 11);
  assert.deepEqual([...b.subarray(h.offset)], [58, 0, 255]);
});

test('a header that is not there is refused', () => {
  assert.equal(readHeader(bytes('256:2000', []), 3), null);
  assert.equal(readHeader('256:2000:7:', 3), null, 'text is not bytes');
  assert.equal(intField('12x'), NaN);
  assert.equal(intField('-3'), -3);
});

test('the byte mappings are the encoder\'s inverse', () => {
  assert.equal(bipolar(0), -1);
  assert.equal(bipolar(255), 1);
  assert.ok(Math.abs(bipolar(128)) < 0.01, '128 is silence');
  assert.equal(unipolar(255), 1);
  assert.equal(unipolar(0), 0);
});

test('a buffer big enough is reused rather than reallocated', () => {
  const a = new Float32Array(8);
  assert.equal(reuse(a, 8), a);
  assert.notEqual(reuse(a, 9), a);
  assert.equal(reuse(null, 4).length, 4);
});
