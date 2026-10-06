// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What every editor's end-to-end test needs: the page, its mock host, and a way
 * to read what the editor said to it.
 *
 * THE MOCK IS THE HOST. Each harness's mock.js records every message the editor
 * sends in `window.__sent`, exactly as iPlug2 would receive it; these helpers
 * decode that record (SAMFUI payloads are base64, as on the wire) so a test
 * asserts on what the PLUGIN would have been told, not on a component's state.
 *
 * NO SLEEPS. Everything waits on a condition -- a message appearing, an
 * attribute changing -- so a slow machine is slower, never red.
 */
import { test as base, expect } from '@playwright/test';
import { writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

export { expect };

/* Mirrors ui-kit/src/lib/shell.js; editor_tags.test.mjs holds that to the C++. */
export const SHELL = { ground: 112, defaults: 113, ready: 120, setText: 121, height: 122 };

export const harnessUrl = (plugin, query = '') =>
  `/plugins/${plugin}/ui/test/harness/index.html${query}`;

/*
 * THE COVERAGE HOOK. With NI_E2E_COVERAGE set, every page records Chrome's own
 * V8 block coverage of the editor bundle, written raw to build/e2e/coverage for
 * scripts/e2e-coverage.mjs to map back to the sources through the bundle's
 * source map. Off by default: an ordinary run pays nothing for it.
 */
const COVERAGE_DIR = process.env.NI_E2E_COVERAGE
  ? fileURLToPath(new URL('../../build/e2e/coverage/', import.meta.url))
  : null;

export const test = base.extend({
  /* Console errors and uncaught exceptions, collected for the whole test. */
  errors: async ({ page }, use) => {
    const errors = [];
    page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
    page.on('pageerror', (e) => errors.push(String(e)));
    await use(errors);
  },
  page: async ({ page }, use, info) => {
    if (COVERAGE_DIR) await page.coverage.startJSCoverage({ resetOnNavigation: false });
    await use(page);
    if (COVERAGE_DIR) {
      const entries = (await page.coverage.stopJSCoverage())
        .filter((e) => /\/ui\/test\/harness\/assets\/ui\.js$/.test(new URL(e.url).pathname));
      mkdirSync(COVERAGE_DIR, { recursive: true });
      const name = `${info.titlePath.join(' ').replace(/[^\w.-]+/g, '_')}-${info.repeatEachIndex}`;
      writeFileSync(join(COVERAGE_DIR, `${name}.json`), JSON.stringify(entries));
    }
  },
});

/** Open a harness and wait for the ready handshake's reply to have landed. */
export async function open(page, plugin, query = '') {
  await page.goto(harnessUrl(plugin, query));
  await expect.poll(() => sentTags(page, 'SAMFUI')).toContain(SHELL.ready);
}

/** Every message the editor sent, with SAMFUI payloads decoded to text. */
export const sent = (page) => page.evaluate(() => (window.__sent ?? []).map((m) => {
  if (m.msg !== 'SAMFUI') return { ...m };
  const bin = atob(m.data ?? '');
  const bytes = Uint8Array.from(bin, (c) => c.charCodeAt(0));
  return { ...m, text: new TextDecoder().decode(bytes) };
}));

export const sentTags = async (page, kind) =>
  (await sent(page)).filter((m) => m.msg === kind).map((m) => m.msgTag);

/** The SAMFUI payloads sent on one tag, as text, oldest first. */
export const texts = async (page, tag) =>
  (await sent(page)).filter((m) => m.msg === 'SAMFUI' && m.msgTag === tag).map((m) => m.text);

/** The SPVFUI values written to one parameter, oldest first. */
export const writes = async (page, idx) =>
  (await sent(page)).filter((m) => m.msg === 'SPVFUI' && m.paramIdx === idx).map((m) => m.value);

/** Forget everything sent so far, so a test asserts on its own gesture only. */
export const clearSent = (page) => page.evaluate(() => { window.__sent.length = 0; });

/** The plugin talking: a parameter value, or a text message on a tag. */
export const pushParam = (page, idx, value) =>
  page.evaluate(([i, v]) => globalThis.SPVFD(i, v), [idx, value]);
export const pushText = (page, tag, text) =>
  page.evaluate(([t, s]) => {
    const b = new TextEncoder().encode(s);
    globalThis.SAMFD(t, b.length, btoa(String.fromCharCode(...b)));
  }, [tag, text]);

/**
 * Drag a knob (a role=slider svg) vertically by `dy` pixels -- negative is up,
 * which is more -- in `steps` moves, calling `during` with the mouse held down.
 */
export async function dragVertically(page, locator, dy, { steps = 8, during } = {}) {
  const box = await locator.boundingBox();
  const x = box.x + box.width / 2, y = box.y + box.height / 2;
  await page.mouse.move(x, y);
  await page.mouse.down();
  await page.mouse.move(x, y + dy / 2, { steps });
  if (during) await during();
  await page.mouse.move(x, y + dy, { steps });
  await page.mouse.up();
}

/** The ground canvas's pixels, hashed -- to tell a still field from a moving one. */
export const groundHash = (page) => page.evaluate(() => {
  const c = document.querySelector('canvas.ground');
  const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
  let h = 2166136261;
  for (let i = 0; i < d.length; i += 4) h = Math.imul(h ^ (d[i] ^ (d[i + 1] << 8) ^ (d[i + 2] << 16)), 16777619);
  return h >>> 0;
});

/**
 * Fake time, installed AND PAUSED: installed alone, Playwright's clock still
 * flows with the wall clock, and a playhead moves between two screenshots.
 * From here on time advances only by `page.clock.runFor`.
 */
export async function freezeClock(page) {
  const t0 = new Date('2026-01-01T00:00:00Z').getTime();
  await page.clock.install({ time: t0 });
  await page.clock.pauseAt(t0 + 1000);
}

/**
 * THE SCREENSHOT'S PREPARATION, and every line of it is determinism.
 *
 *   - Motion off, through the editor's own remembered setting, BEFORE the page
 *     loads: the ground then draws its static design and never animates.
 *   - Playwright's clock installed at a fixed instant, so every setInterval the
 *     mock runs (the scope, the meter, the column feed) and every
 *     requestAnimationFrame the editor asks for advance only when told to.
 *   - Then a fixed amount of that fake time, so the picture is the same frame
 *     on every run on every machine.
 */
export async function openForScreenshot(page, plugin, motionKey, query = '') {
  await page.addInitScript((key) => {
    try { localStorage.setItem(`ultraviolet.motion.${key}`, '0'); } catch { /* none */ }
  }, motionKey);
  await freezeClock(page);
  await page.goto(harnessUrl(plugin, query));
  await page.clock.runFor(2000);
  await expect.poll(() => sentTags(page, 'SAMFUI')).toContain(SHELL.ready);
  await page.evaluate(() => document.fonts.ready);
  await page.clock.runFor(500);
}

/**
 * EVERYTHING THAT WOULD MAKE A PLUGIN WINDOW LOOK BROKEN, as a list of
 * sentences -- empty when there is none: the document, or any element in it,
 * scrolled away from 0,0 (a focused field scrolled into view shifts the whole
 * editor sideways and cuts it off), or an element whose box reaches past the
 * window. A box with no size is skipped; it draws nothing.
 */
export const layoutBreaches = (page) => page.evaluate(() => {
  const out = [];
  const name = (el) => `<${el.tagName.toLowerCase()}${el.className?.baseVal ?? el.className
    ? ` class="${el.className?.baseVal ?? el.className}"` : ''}${
    el.getAttribute('aria-label') ? ` aria-label="${el.getAttribute('aria-label')}"` : ''}>`;
  const doc = document.scrollingElement;
  if (doc.scrollLeft || doc.scrollTop) out.push(`the document scrolled to ${doc.scrollLeft},${doc.scrollTop}`);
  const W = window.innerWidth, H = window.innerHeight;
  for (const el of document.body.querySelectorAll('*')) {
    if (el.scrollLeft || el.scrollTop) out.push(`${name(el)} scrolled to ${el.scrollLeft},${el.scrollTop}`);
    const r = el.getBoundingClientRect();
    if (r.width === 0 && r.height === 0) continue;
    /* Half a pixel: a scaled window's edges land between device pixels. */
    if (r.left < -0.5 || r.top < -0.5 || r.right > W + 0.5 || r.bottom > H + 0.5)
      out.push(`${name(el)} reaches ${Math.round(r.left)},${Math.round(r.top)} .. ${
        Math.round(r.right)},${Math.round(r.bottom)} outside ${W}x${H}`);
  }
  return out;
});

/** Two frames: whatever a click or a focus set off has been laid out. */
export const settle = (page) => page.evaluate(() => new Promise((r) =>
  requestAnimationFrame(() => requestAnimationFrame(r))));
