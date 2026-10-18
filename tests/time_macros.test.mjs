// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NO BUILD READS THE CLOCK.
 *
 * cmake/NiCcache.cmake gives ccache `sloppiness=time_macros`, so a compile
 * that names __DATE__ or __TIME__ can be served from the cache with another
 * day's date in it. That is safe only while nothing of ours reads one: JUCE's
 * single use, juce_core_CompilationTime.cpp, is read by
 * Time::getCompilationDate() alone. This holds every C, C++ and Objective-C
 * source the repository owns to that -- no time macro, no call to it -- so a
 * stale stamp can never be seen.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const SOURCE = /\.(c|cc|cpp|cxx|h|hh|hpp|hxx|m|mm|inc)$/;
const CLOCK = /__DATE__|__TIME__|__TIMESTAMP__|getCompilationDate/;

const owned = () =>
  execFileSync('git', ['ls-files', '-z'], { cwd: ROOT, encoding: 'utf8' })
    .split('\0')
    .filter((f) => SOURCE.test(f) && !f.startsWith('external/') && f !== 'tests/time_macros.test.mjs');

test('no owned source reads the time it was compiled at', () => {
  const files = owned();
  assert.ok(files.length > 100, `only ${files.length} sources found -- this test reads \`git ls-files\``);
  const readers = files.filter((f) => CLOCK.test(readFileSync(join(ROOT, f), 'utf8')));
  assert.deepEqual(readers, [], 'these read the compile time, which ccache may serve from another day');
});
