#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The native UI's design tokens, generated from the design system's own files.
 *
 *   node scripts/gen-tokens.mjs           write what is stale
 *   node scripts/gen-tokens.mjs --check   say what is stale, exit 1 if anything is
 *
 * WHAT IT READS, all of it inside the mirror of the published Ultraviolet
 * system (design/scheme/project), which stays byte for byte what was published:
 *
 *   tokens.json              every colour, space, radius, size, stroke, shadow
 *                            and text style, with the system's usage notes
 *   components/ground.js     the Ground's motion constants: UVGround.defaults
 *                            and UVGround.beatDefaults, the reference
 *                            implementation's published options. tokens.json
 *                            has no motion family, and these are the only
 *                            durations and rates the system states as data
 *   components/bundle.css    the window ground's grain: a 96px PNG tile the
 *                            stylesheet carries as a data URI
 *
 * WHAT IT WRITES:
 *
 *   plugins/_shared/ui/src/UvTokens.h          namespace uv::tok
 *   plugins/_shared/ui/assets/ground-grain.png the grain tile, byte for byte
 *
 * GENERATED, AND CHECKED IN, AND HELD TO THE GENERATOR. The ui-kit's tokens.css
 * is a transcription that a test compares with tokens.json; a C++ header has
 * no such second reader, and a transcription nobody compares drifts. So this
 * one is written by a program, committed so that a build needs no node, and
 * tests/ui_tokens_native.test.mjs fails while the committed files differ from
 * what this would write. Re-sync the design, run this, commit both.
 *
 * NOTHING IS GUESSED. A token shape this does not know -- a colour that is
 * neither #rrggbb nor #rrggbbaa, a per-theme value naming a theme the system
 * does not declare, a family the header has no place for -- stops it with the
 * token's name, rather than writing something plausible.
 */
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import vm from 'node:vm';

export const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const DESIGN = join(ROOT, 'design', 'scheme', 'project');
const TOKENS_JSON = join(DESIGN, 'tokens.json');
const GROUND_JS = join(DESIGN, 'components', 'ground.js');
const BUNDLE_CSS = join(DESIGN, 'components', 'bundle.css');

export const TOKENS_H = join(ROOT, 'plugins', '_shared', 'ui', 'src', 'UvTokens.h');
export const GRAIN_PNG = join(ROOT, 'plugins', '_shared', 'ui', 'assets', 'ground-grain.png');

const fail = (msg) => { throw new Error(`gen-tokens: ${msg}`); };

/* ------------------------------------------------------------- names -- */

/* "bg-000" -> "bg000", "ink-muted" -> "inkMuted", "control-h" -> "controlH":
 * the token's own name, camel-cased, so a search for either spelling finds the
 * other. A name that does not make an identifier stops the generator. */
export function identifier(name) {
  const parts = name.split('-');
  const id = parts[0] + parts.slice(1)
    .map((p) => (/^[0-9]/.test(p) ? p : p[0].toUpperCase() + p.slice(1))).join('');
  if (!/^[a-z][A-Za-z0-9]*$/.test(id)) fail(`token "${name}" does not make a C++ identifier`);
  return id;
}

/* ----------------------------------------------------------- colours -- */

/* #rrggbb or #rrggbbaa -> 0xAARRGGBB, which is juce::Colour's order. */
export function argb(value, name) {
  const m = /^#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?$/.exec(String(value).trim());
  if (!m) fail(`colour "${name}" is ${JSON.stringify(value)}, not #rrggbb or #rrggbbaa`);
  return `0x${(m[2] ?? 'ff').toLowerCase()}${m[1].toLowerCase()}`;
}

/* A px length ("4px") or a bare number ("48") -> a number. */
function px(value, name) {
  const m = /^(-?[0-9]*\.?[0-9]+)(px)?$/.exec(String(value).trim());
  if (!m) fail(`"${name}" is ${JSON.stringify(value)}, not a length in px`);
  return Number(m[1]);
}

/* An em length ("0.08em", "-.01em") -> a number of ems. */
function em(value, name) {
  if (value === undefined) return 0;
  const m = /^(-?[0-9]*\.?[0-9]+)em$/.exec(String(value).trim());
  if (!m) fail(`letter-spacing of "${name}" is ${JSON.stringify(value)}, not in em`);
  return Number(m[1]);
}

/* A float literal C++ reads back as exactly this number, with its f. */
export function float(n) {
  if (!Number.isFinite(n)) fail(`${n} is not a finite number`);
  let s = String(n);
  if (!/[.e]/.test(s)) s += '.0';
  return `${s}f`;
}

/*
 * The CSS shadow list the system writes -- "0 0 10px #a259ff, 0 0 2px #efe3ff"
 * -- as layers: x, y, blur, spread, colour. Split on the commas between
 * layers only; a colour here is always hex, so there is no rgba() comma to
 * survive, and one that is not hex stops the generator in argb().
 */
function shadowLayers(value, name) {
  return String(value).split(',').map((layer) => {
    const parts = layer.trim().split(/\s+/);
    const colour = parts.pop();
    const lengths = parts.map((p) => px(p, name));
    if (lengths.length < 2 || lengths.length > 4)
      fail(`shadow "${name}" has a layer "${layer.trim()}" this cannot read`);
    const [x, y, blur = 0, spread = 0] = lengths;
    return { x, y, blur, spread, argb: argb(colour, name) };
  });
}

/* ------------------------------------------------------------ prose -- */

/* A usage note as comment lines, wrapped, with nothing that could end the
 * comment early. */
function comment(text, indent = '', width = 78) {
  const words = String(text).replace(/\*\//g, '* /').split(/\s+/).filter(Boolean);
  const lines = [];
  let line = '';
  for (const w of words) {
    if (line && (indent.length + 3 + line.length + 1 + w.length) > width) {
      lines.push(line);
      line = w;
    } else {
      line = line ? `${line} ${w}` : w;
    }
  }
  if (line) lines.push(line);
  if (lines.length === 1 && indent.length + lines[0].length + 6 <= width)
    return `${indent}/* ${lines[0]} */`;
  return [`${indent}/*`, ...lines.map((l) => `${indent} * ${l}`), `${indent} */`].join('\n');
}

/* ----------------------------------------------------------- motion -- */

/*
 * ground.js, run in a context of its own with a stand-in `window`: it is a
 * plain script that hangs UVGround on whatever it is handed, and does nothing
 * at load but define functions and two option objects. Those objects are the
 * reference implementation's published API (the Ground README names
 * UVGround.defaults and UVGround.beatDefaults), which is what makes them data
 * rather than code this happens to be able to read.
 */
export function readMotion(source = readFileSync(GROUND_JS, 'utf8')) {
  const sandbox = { window: {} };
  vm.runInNewContext(source, sandbox, { filename: 'ground.js', timeout: 1000 });
  const g = sandbox.window.UVGround;
  if (!g || typeof g.defaults !== 'object' || typeof g.beatDefaults !== 'object')
    fail('ground.js no longer publishes UVGround.defaults and UVGround.beatDefaults');
  /* The values come from running the file; the notes beside them only from
   * its text, where each option carries a `// what it is` -- read for the
   * header's comments and nothing else. */
  const notes = { ground: {}, beat: {} };
  for (const block of source.matchAll(/var\s+(DEFAULTS|BEAT)\s*=\s*\{([\s\S]*?)\n\s*\};/g)) {
    const into = notes[block[1] === 'DEFAULTS' ? 'ground' : 'beat'];
    for (const m of block[2].matchAll(/^\s*(\w+):\s*[^\n]*?\/\/\s*(.+)$/gm)) into[m[1]] = m[2].trim();
  }
  return {
    version: String(g.version),
    ground: { ...g.defaults },
    beat: { ...g.beatDefaults },
    notes,
  };
}

/* ------------------------------------------------------------ grain -- */

/*
 * The grain tile, out of the stylesheet. bundle.css carries it more than once
 * (the .ph root and .ph-window); every copy must be the same image, or the
 * design does not say which grain it means.
 */
export function readGrain(css = readFileSync(BUNDLE_CSS, 'utf8')) {
  const uris = [...css.matchAll(/data:image\/png;base64,([A-Za-z0-9+/=]+)/g)].map((m) => m[1]);
  if (!uris.length) fail('bundle.css carries no PNG for the ground grain');
  if (new Set(uris).size !== 1) fail('bundle.css carries more than one different PNG');
  const png = Buffer.from(uris[0], 'base64');
  if (png.readUInt32BE(0) !== 0x89504e47 || png.toString('latin1', 12, 16) !== 'IHDR')
    fail('the grain in bundle.css is not a PNG');
  const w = png.readUInt32BE(16), h = png.readUInt32BE(20);
  /* 96 because the dot pitch (12) divides it, so one tile of dots and grain
   * together repeats exactly. */
  if (w !== 96 || h !== 96) fail(`the grain tile is ${w}x${h}, not 96x96`);
  return png;
}

/* ----------------------------------------------------------- header -- */

export function renderHeader(system, motion) {
  const out = [];
  const emit = (s = '') => out.push(s);

  const release = system.meta?.release;
  if (!release) fail('tokens.json has no meta.release');
  if (motion.version !== release)
    fail(`ground.js is ${motion.version} and tokens.json is ${release}: re-sync the design`);

  /* ---- colour: one namespace per theme, values per theme where the system
   * gives them per theme. */
  const themes = system.color?.themes ?? [];
  if (!themes.length) fail('tokens.json declares no colour theme');
  const themeIds = themes.map((t) => t.id);
  const colours = system.color.tokens.map((t) => {
    const per = {};
    for (const id of themeIds) {
      let v = t.value;
      if (v && typeof v === 'object') {
        for (const k of Object.keys(v))
          if (!themeIds.includes(k)) fail(`colour "${t.name}" has a value for theme "${k}", which is not declared`);
        v = v[id];
        if (v === undefined) fail(`colour "${t.name}" has no value for theme "${id}"`);
      }
      per[id] = argb(v, t.name);
    }
    return { name: t.name, id: identifier(t.name), usage: t.usage, per };
  });
  const seen = new Set();
  for (const c of colours) {
    if (seen.has(c.id)) fail(`two colours make the identifier ${c.id}`);
    seen.add(c.id);
  }
  const defaultTheme = themeIds[0];
  /* A shadow's colour, named after the token it is, in the default theme. */
  const colourRef = (value) => {
    const hit = colours.find((c) => c.per[defaultTheme] === value);
    return hit ? `argb::${defaultTheme}::${hit.id}` : value;
  };

  /* ---- the ground's colours must be the tokens they stand for. */
  const rgb = (a) => `0xff${a.map((v) => v.toString(16).padStart(2, '0')).join('')}`;
  const g = motion.ground;
  const want = { bg: 'bg-000', dot: 'bg-dot', grain: 'uv-deep' };
  for (const [key, token] of Object.entries(want)) {
    const c = colours.find((x) => x.name === token);
    if (!c) fail(`ground.js colours "${key}" as ${token}, which tokens.json does not define`);
    if (!Array.isArray(g[key]) || rgb(g[key]) !== c.per[defaultTheme])
      fail(`ground.js's ${key} is not ${token}: the design disagrees with itself`);
  }

  emit('// SPDX-License-Identifier: GPL-3.0-or-later');
  emit('// Copyright (C) 2026 Torben Gräber');
  emit();
  emit('/*');
  emit(` * ${system.name} ${release} -- GENERATED, DO NOT EDIT.`);
  emit(' *');
  emit(' * scripts/gen-tokens.mjs wrote this from design/scheme/project/tokens.json');
  emit(' * and the motion constants of design/scheme/project/components/ground.js.');
  emit(' * After re-syncing the design, run');
  emit(' *');
  emit(' *     node scripts/gen-tokens.mjs');
  emit(' *');
  emit(' * and commit what it writes. tests/ui_tokens_native.test.mjs fails while this');
  emit(' * file differs from what the generator would write, and while any other');
  emit(' * source of the native UI spells a colour of its own: this is the one file in');
  emit(' * it that may.');
  emit(' *');
  emit(' * Names are the system\'s, camel-cased: bg-000 is bg000, ink-muted inkMuted,');
  emit(' * control-h controlH. Lengths are CSS px as floats; a text style\'s tracking');
  emit(' * is in em, as the system states it. The comments are the system\'s usage');
  emit(' * notes, which say what a token is FOR.');
  emit(' */');
  emit('#pragma once');
  emit();
  emit('#include <juce_graphics/juce_graphics.h>');
  emit();
  emit('namespace uv::tok');
  emit('{');
  emit();
  emit('/* The system these values are, and its release. */');
  emit(`inline constexpr const char* systemName = ${JSON.stringify(system.name)};`);
  emit(`inline constexpr const char* release = ${JSON.stringify(release)};`);
  emit();

  /* ---- colour */
  emit('/* ============================================================ colour == */');
  emit();
  if (system.color.note) emit(comment(system.color.note));
  emit();
  emit('/*');
  emit(' * Each theme twice: the 0xAARRGGBB words, which constexpr tables can use, and');
  emit(' * the juce::Colour constants built from them, which everything else uses.');
  emit(` * \`colour\` is the theme the plugins draw in${themeIds.length === 1 ? ': the system has one' : ''}.`);
  emit(' */');
  emit('namespace argb');
  emit('{');
  for (const id of themeIds) {
    emit(`namespace ${id}`);
    emit('{');
    for (const c of colours)
      emit(`inline constexpr juce::uint32 ${c.id} = ${c.per[id]};`);
    emit('}');
  }
  emit('}');
  emit();
  for (const id of themeIds) {
    const theme = themes.find((t) => t.id === id);
    emit(`/* The "${theme.name ?? id}" theme. */`);
    emit(`namespace ${id}`);
    emit('{');
    for (const c of colours) {
      if (c.usage) emit(comment(c.usage));
      emit(`inline const juce::Colour ${c.id} { argb::${id}::${c.id} };`);
    }
    emit('}');
    emit();
  }
  emit(`namespace colour = ${defaultTheme};`);
  emit();

  /* ---- the length families */
  const lengths = [
    ['spacing', 'space'], ['radius', 'radius'], ['size', 'size'], ['stroke', 'stroke'],
  ];
  const known = new Set(['color', 'type', 'shadow', 'name', 'version', 'meta', ...lengths.map((l) => l[0])]);
  for (const key of Object.keys(system))
    if (!known.has(key)) fail(`tokens.json has a family "${key}" this generator does not know`);

  for (const [family, ns] of lengths) {
    const block = system[family];
    if (!block) fail(`tokens.json has no ${family} family`);
    emit(`/* ${'='.repeat(Math.max(4, 64 - family.length))} ${family} == */`);
    emit();
    if (block.note) emit(comment(block.note));
    emit(`namespace ${ns}`);
    emit('{');
    for (const t of block.tokens) {
      if (t.usage) emit(comment(t.usage));
      emit(`inline constexpr float ${identifier(t.name)} = ${float(px(t.value, t.name))};`);
    }
    emit('}');
    emit();
  }

  /* ---- shadows */
  const shadows = system.shadow;
  if (!shadows) fail('tokens.json has no shadow family');
  emit('/* ============================================================ shadow == */');
  emit();
  if (shadows.note) emit(comment(shadows.note));
  emit();
  emit('/* One layer of a CSS box-shadow: offsets, blur and spread in px, and its');
  emit(' * colour. A shadow is its layers in the order CSS lists them. */');
  emit('struct ShadowLayer');
  emit('{');
  emit('    float x, y, blur, spread;');
  emit('    juce::uint32 argb;');
  emit('};');
  emit();
  emit('namespace shadow');
  emit('{');
  for (const t of shadows.tokens) {
    if (t.usage) emit(comment(t.usage));
    const layers = shadowLayers(t.value, t.name).map((l) =>
      `    { ${float(l.x)}, ${float(l.y)}, ${float(l.blur)}, ${float(l.spread)}, ${colourRef(l.argb)} },`);
    emit(`inline constexpr ShadowLayer ${identifier(t.name)}[] = {`);
    layers.forEach((l) => emit(l));
    emit('};');
  }
  emit('}');
  emit();

  /* ---- type */
  const type = system.type;
  if (!type?.families?.mono) fail('tokens.json has no mono family');
  const stack = type.families.mono;
  const first = /^\s*"([^"]+)"|^\s*([^,]+)/.exec(stack);
  const family = (first[1] ?? first[2]).trim();
  emit('/* ============================================================== type == */');
  emit();
  if (type.note) emit(comment(type.note));
  emit();
  emit('/* A text style as the system states it: size and line height in px (CSS');
  emit(' * px, so the size is the em square), weight as CSS numbers it, tracking in');
  emit(' * em. Uppercase is not a token; the styles that set it say so in their');
  emit(' * usage notes. */');
  emit('struct TextStyle');
  emit('{');
  emit('    float size, lineHeight;');
  emit('    int weight;');
  emit('    float tracking;');
  emit('};');
  emit();
  emit('namespace type');
  emit('{');
  emit('/* The family the plugins bundle and draw in: the first of the system\'s');
  emit(' * stack, which names the fallbacks for a page that cannot bundle it. */');
  emit(`inline constexpr const char* family = ${JSON.stringify(family)};`);
  emit(`inline constexpr const char* familyStack = ${JSON.stringify(stack)};`);
  emit();
  const weights = new Set();
  const styleIds = new Set();
  for (const group of type.groups ?? []) {
    if (group.family !== 'mono') fail(`type group "${group.name}" is not in the mono family`);
    for (const s of group.styles) {
      const id = identifier(s.name);
      if (styleIds.has(id)) fail(`two text styles make the identifier ${id}`);
      styleIds.add(id);
      weights.add(Number(s.fontWeight));
      if (s.usage) emit(comment(s.usage));
      emit(`inline constexpr TextStyle ${id} { ${float(px(s.fontSize, s.name))}, ` +
           `${float(px(s.lineHeight, s.name))}, ${Number(s.fontWeight)}, ${float(em(s.letterSpacing, s.name))} };`);
    }
  }
  emit();
  emit('/* Every weight a style uses: the faces a plugin has to bundle. */');
  emit(`inline constexpr int weights[] = { ${[...weights].sort((a, b) => a - b).join(', ')} };`);
  emit('}');
  emit();

  /* ---- motion */
  const numberOrBool = (v, key) => {
    if (typeof v === 'boolean') return ['bool', String(v)];
    if (typeof v === 'number') return ['float', float(v)];
    fail(`ground.js option "${key}" is ${JSON.stringify(v)}, which is neither a number nor a switch`);
    return null;
  };
  emit('/* ============================================================ motion == */');
  emit();
  emit('/*');
  emit(' * "Controls never animate." The one thing that moves is the Ground, and these');
  emit(' * are its reference implementation\'s options (ground.js, UVGround.defaults and');
  emit(' * UVGround.beatDefaults): durations in seconds, rates in Hz or per second,');
  emit(' * lengths in px, opacities 0..1. The ground\'s colours are not repeated here:');
  emit(' * ground.js names bg-000, bg-dot and uv-deep, and the generator checks that');
  emit(' * it still does.');
  emit(' */');
  emit('namespace motion');
  emit('{');
  const heads = {
    ground: 'The field: UVGround.defaults.',
    beat: 'The clock that turns the host\'s transport into rings: UVGround.beatDefaults.',
  };
  for (const [ns, obj, skip] of [['ground', motion.ground, Object.keys(want)], ['beat', motion.beat, []]]) {
    emit(comment(heads[ns]));
    emit(`namespace ${ns}`);
    emit('{');
    for (const [key, v] of Object.entries(obj)) {
      if (skip.includes(key)) continue;
      if (!/^[a-z][A-Za-z0-9]*$/.test(key)) fail(`ground.js option "${key}" is not an identifier`);
      const [t, lit] = numberOrBool(v, key);
      const note = motion.notes?.[ns]?.[key];
      emit(`inline constexpr ${t} ${key} = ${lit};${note ? `  // ${note.replace(/\*\//g, '* /')}` : ''}`);
    }
    emit('}');
  }
  emit('}');
  emit();
  emit('} // namespace uv::tok');
  emit();
  return out.join('\n');
}

/* --------------------------------------------------------- generate -- */

/** Every file the generator owns, as { absolute path: Buffer }. */
export function generate() {
  const system = JSON.parse(readFileSync(TOKENS_JSON, 'utf8'));
  const header = renderHeader(system, readMotion());
  return new Map([
    [TOKENS_H, Buffer.from(header, 'utf8')],
    [GRAIN_PNG, readGrain()],
  ]);
}

/*
 * The files on disk that differ from what generate() makes: byte for byte,
 * except that a text file checked out with CRLF line ends (git's autocrlf on
 * Windows) is the same file.
 */
export function stale(files = generate()) {
  const same = (path, bytes) => {
    if (!existsSync(path)) return false;
    const disk = readFileSync(path);
    if (!path.endsWith('.h')) return disk.equals(bytes);
    return disk.toString('utf8').replace(/\r\n/g, '\n') === bytes.toString('utf8');
  };
  return [...files].filter(([path, bytes]) => !same(path, bytes)).map(([path]) => path);
}

function main(argv) {
  const files = generate();
  const out = stale(files);
  if (argv.includes('--check')) {
    for (const p of out) console.error(`stale: ${relative(ROOT, p)}`);
    if (out.length) console.error('run: node scripts/gen-tokens.mjs');
    return out.length ? 1 : 0;
  }
  for (const p of out) {
    mkdirSync(dirname(p), { recursive: true });
    writeFileSync(p, files.get(p));
    console.log(`wrote ${relative(ROOT, p)}`);
  }
  if (!out.length) console.log('up to date');
  return 0;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href)
  process.exitCode = main(process.argv.slice(2));
