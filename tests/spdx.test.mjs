// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Every source file this repository owns opens with its licence and its
 * copyright.
 *
 * WHY A TEST. The licence moved from MIT to GPL-3.0-or-later
 * (docs/adr/0001-gpl-3.0-or-later.md), and on the day it did, about 120 source
 * files carried no notice at all. A notice that has to be remembered is
 * missing from the next new file; one that a test demands is not. The SPDX
 * line is also what a machine reads -- a licence scanner, a distribution's
 * audit -- so both lines are held exactly, in the file's own comment syntax:
 *
 *   slash   .rs .c .h .cpp .mm .m .js .jsx .mjs .ts, and .astro inside the
 *           frontmatter fence that opens it:
 *               // SPDX-License-Identifier: GPL-3.0-or-later
 *               // Copyright (C) 2026 Torben Gräber
 *   css     .css: the same two lines, each in a C comment of its own
 *   hash    .sh .cmake .py .yml, CMakeLists.txt, Dockerfile:
 *               # SPDX-License-Identifier: GPL-3.0-or-later
 *               # Copyright (C) 2026 Torben Gräber
 *
 * A shebang stays the first line, and the pair follows it. A generated source
 * file is still ours: its generator writes the two lines.
 *
 *   node --test tests/spdx.test.mjs
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFileSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { basename, dirname, extname, join } from 'node:path';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');

const SPDX = 'SPDX-License-Identifier: GPL-3.0-or-later';
const COPYRIGHT = 'Copyright (C) 2026 Torben Gräber';

/* One line of a notice, in each comment syntax. */
const LINE = {
  slash: (text) => `// ${text}`,
  css: (text) => `/* ${text} */`,
  hash: (text) => `# ${text}`,
};

/* What a source file is: its extension, or its whole name. */
const BY_EXTENSION = {
  '.rs': 'slash', '.c': 'slash', '.h': 'slash', '.cpp': 'slash', '.mm': 'slash',
  '.m': 'slash', '.js': 'slash', '.jsx': 'slash', '.mjs': 'slash', '.ts': 'slash',
  '.astro': 'slash',
  '.css': 'css',
  '.sh': 'hash', '.cmake': 'hash', '.py': 'hash', '.yml': 'hash',
};
const BY_NAME = { 'CMakeLists.txt': 'hash', Dockerfile: 'hash' };
const syntaxOf = (file) => BY_NAME[basename(file)] ?? BY_EXTENSION[extname(file)];

/*
 * WHAT IS NOT OURS TO ANNOTATE, each with its reason.
 *
 * Untracked trees -- node_modules, build*, target, dist -- need no entry,
 * because git does not list them. Neither do test fixtures, goldens, oracle
 * tables, lockfiles and JSON: they are .txt, .json, .lock or binary, which no
 * syntax above covers, and JSON has no comment to put a notice in.
 */
const EXCLUDED = [
  [/^external\//,
   'vendored code and the iPlug2 submodule: third-party code keeps its own notices'],
  [/^design\//,
   'the mirrors of the two design artifacts, which stay byte for byte what was published'],
  [/^ui-kit\//,
   'the Solid kit, deleted with the web editors later in the JUCE refactor'],
  [/^plugins\/[^/]+\/ui\//,
   'the Solid editors, deleted with the kit'],
  [/^engines\/trance-gate\/include\/(plugin_api_v1|audio_fx_api_v2)\.h$/,
   "Schwung's module-API headers, copied unmodified: they keep their origin, " +
   'which THIRD_PARTY_LICENSES.md records'],
];

const tracked = () =>
  execFileSync('git', ['ls-files', '-z'], { cwd: ROOT, encoding: 'utf8' })
    .split('\0')
    .filter(Boolean);

/* A file deleted but not yet staged is tracked and absent: nothing to read. */
const owned = () =>
  tracked()
    .filter((f) => syntaxOf(f) && !EXCLUDED.some(([re]) => re.test(f)))
    .filter((f) => statSync(join(ROOT, f), { throwIfNoEntry: false })?.isFile());

/* The two lines that must be the notice: the first that can hold a comment,
 * which is after a shebang, and inside an .astro file's opening fence. */
function headOf(file, text) {
  const lines = text.split('\n');
  const at = lines[0]?.startsWith('#!') || (file.endsWith('.astro') && lines[0] === '---') ? 1 : 0;
  return lines.slice(at, at + 2);
}

test('every owned source file opens with its licence and its copyright', () => {
  const files = owned();
  assert.ok(files.length > 200,
    `only ${files.length} owned source files found -- this test reads \`git ls-files\``);
  const wrong = [];
  for (const f of files) {
    const line = LINE[syntaxOf(f)];
    const want = [line(SPDX), line(COPYRIGHT)];
    const got = headOf(f, readFileSync(join(ROOT, f), 'utf8'));
    if (got[0] !== want[0] || got[1] !== want[1])
      wrong.push(`${f}\n      wants  ${want.join('  /  ')}\n      has    ${got.join('  /  ')}`);
  }
  assert.deepEqual(wrong, [],
    `${wrong.length} source file(s) do not open with the licence notice. Put these two ` +
    'lines first (after a shebang; inside the fence in .astro), in the file\'s comment ' +
    `syntax:\n  ${wrong.join('\n  ')}`);
});

test('no owned file still carries the MIT notice it had before', () => {
  const stale = [];
  for (const f of owned()) {
    readFileSync(join(ROOT, f), 'utf8').split('\n').forEach((text, i) => {
      if (/Torben Gräber[.,]?\s+MIT\b/.test(text)) stale.push(`${f}:${i + 1}  ${text.trim()}`);
    });
  }
  assert.deepEqual(stale, [], 'a second, contradicting notice survived the relicensing');
});

/* The Side-Chain's MIDI trigger is a port of MIT code. MIT allows it in a GPLv3
 * work on one condition -- its notice stays -- so the notice stays where a
 * person reading the file meets it: beside ours, before the code. */
test('the schwung-ducker port keeps its upstream notice beside ours', () => {
  const file = 'engines/side-chain/crates/sc-core/src/midi.rs';
  const head = readFileSync(join(ROOT, file), 'utf8').split('\n').slice(0, 8).join('\n');
  assert.match(head, /MIT License, Copyright \(c\) 2026 Charles Vestal/,
    `${file} lost schwung-ducker's notice from its header`);
});

/* An exclusion that matches nothing is a hole waiting for a file to fall
 * into: the day ui-kit/ is deleted, its entry goes too. */
test('every exclusion still names something tracked', () => {
  const files = tracked();
  const stale = EXCLUDED.filter(([re]) => !files.some((f) => re.test(f)))
    .map(([re, why]) => `${re}  (${why})`);
  assert.deepEqual(stale, [], 'exclusions that match no tracked file -- remove them');
});
