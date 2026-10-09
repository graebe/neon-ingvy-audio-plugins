// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Every line the hint bar can show (src/lib/info.js) is in the system's form
 * and fits the bar: "Name — what it does", at most 72 characters, as the
 * native editor holds its own lines (InfoLines.h).
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { INFO } from '../src/lib/info.js';

const lines = (o) => Object.values(o).flatMap((v) => (typeof v === 'string' ? [v] : lines(v)));

test('every info line is a name, a dash and what it does, in 72 characters', () => {
  const all = lines(INFO);
  assert.ok(all.length > 30);
  for (const l of all) {
    assert.match(l, /^\S.* — \S/, l);
    assert.ok([...l].length <= 72, `${[...l].length}: ${l}`);
  }
});
