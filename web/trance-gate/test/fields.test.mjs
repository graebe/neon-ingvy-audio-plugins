// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The readouts' fields as this editor reads them, against the engine's own
 * names for them (tg-core's UI_FIELDS / PARAMS_FIELDS, answered by
 * get_param("ui_fields") / ("params_fields")). The editor reads by position;
 * the engine promises to append only. This is where a field moved or renamed
 * fails, instead of a ring that quietly draws the cursor as the phase.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { loadEngine } from './support/engine.mjs';
import { UI_FIELDS } from '../src/lib/readouts.js';
import { PARAMS_FIELDS } from '../src/lib/move.js';

async function engineNames(key) {
  const c = await loadEngine();
  const core = c.fn.tg_core_create(44100);
  try {
    const text = c.text(c.withString(key, (k) => c.withBuffer(512, (out, n) => c.fn.tg_core_get_param(core, k, out, n))));
    return text.split(':');
  } finally {
    c.fn.tg_core_destroy(core);
  }
}

test('the editor reads `ui` by the engine\'s field order', async () => {
  const names = await engineNames('ui_fields');
  assert.deepEqual(names.slice(0, UI_FIELDS.length), UI_FIELDS);
});

test('the editor reads `params` by the engine\'s field order', async () => {
  const names = await engineNames('params_fields');
  assert.deepEqual(names.slice(0, PARAMS_FIELDS.length), PARAMS_FIELDS.map((k, i) => k ?? names[i]));
});
