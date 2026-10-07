// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The iPlug2 bridge, given nothing.
 *
 * The shell sends an empty payload as SAMFD(tag, 0, '') (ni::WebPlugin::Send):
 * iPlug2's own call printed a null pointer there, and "(null)" is not base64,
 * so it reached a fresh Listen-In's name field as text. These hold the page's
 * half -- an empty string is an empty message, to a text tag and a byte tag
 * alike.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { onMessage, onBytes } from '../src/lib/iplug.js';

test('an empty payload is empty text, not a pass-through', () => {
  const seen = [];
  const off = onMessage((tag, text) => seen.push([tag, text]));
  globalThis.SAMFD(96, 0, '');
  off();
  assert.deepEqual(seen, [[96, '']]);
});

test('an empty payload to a binary tag is zero bytes', () => {
  const seen = [];
  const off = onBytes(70, (b) => seen.push(b));
  globalThis.SAMFD(70, 0, '');
  off();
  assert.equal(seen.length, 1);
  assert.ok(seen[0] instanceof Uint8Array);
  assert.equal(seen[0].length, 0);
});
