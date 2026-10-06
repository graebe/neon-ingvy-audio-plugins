// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Spectrogram's editor, end to end against its mock host.
 *
 * The one editor with no host parameters: what it sends are the session's
 * messages -- which buses to open, what the picture is of, what the clash
 * compares -- and the rule under test is that it sends NONE of them until the
 * plugin's saved session (tag 69) has been applied, or it would overwrite it.
 *
 * The mock lists two Listen-Ins, Bass on bus 1 and Pad on bus 4, so the
 * channels are input (0), Bass (1) and Pad (2).
 */
import {
  test, expect, open, openForScreenshot, sent, texts, clearSent, SHELL,
} from './harness.mjs';

const W = 720, H = 502;
/* lib/msg.js */
const MSG = { range: 96, select: 97, clash: 98, view: 99, compare: 100 };
const SESSION = Object.values(MSG);

test.use({ viewport: { width: W, height: H } });

const pushes = async (page) => (await sent(page))
  .filter((m) => m.msg === 'SAMFUI' && SESSION.includes(m.msgTag))
  .map((m) => `${m.msgTag} ${m.text}`);

const clash = (page) => page.getByRole('switch', { name: 'clash' });

test('loads without a console error and completes the ready handshake', async ({ page, errors }) => {
  await open(page, 'spectrogram');
  /* The axis arrives in the reply to `ready`; the hint prints its span, from
   * the lowest band's centre to the highest's. */
  await expect(page.locator('.hint-bar')).toContainText('10 Hz – 19.7 kHz');
  await expect(page.locator('.stale')).toHaveText('');
  expect((await sent(page)).filter((m) => m.msgTag === SHELL.ready)).toHaveLength(1);
  /* Opening pushes nothing: the session is the plugin's. */
  expect(await pushes(page)).toEqual([]);
  expect(errors).toEqual([]);
});

/*
 * THE PICTURE MAY NOT WAIT FOR A FRAME CALLBACK. A WKWebView that takes its
 * window for hidden -- occluded, or a host window WebKit misjudges -- stops
 * servicing requestAnimationFrame while the plugin's columns keep arriving.
 * The repaint was gated on it, so the history filled and the screen stayed
 * the floor colour. Here no frame callback ever runs, and the columns the
 * mock sends must still reach the visible canvas.
 */
test('columns reach the picture when no animation frame is ever serviced', async ({ page }) => {
  await page.addInitScript(() => {
    globalThis.requestAnimationFrame = () => 1;
    globalThis.cancelAnimationFrame = () => {};
  });
  await open(page, 'spectrogram');
  const colours = () => page.evaluate(() => {
    const c = document.querySelector('.spectro-canvas');
    const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
    const seen = new Set();
    for (let i = 0; i < d.length; i += 4) seen.add((d[i] << 16) | (d[i + 1] << 8) | d[i + 2]);
    return seen.size;
  });
  /* The floor alone is one colour; the mock's sweep and haze are dozens. */
  await expect.poll(colours, { timeout: 5000 }).toBeGreaterThan(8);
});

/*
 * THE PICTURE MAY NOT DEPEND ON THE STYLESHEET HAVING WON THE RACE. In a
 * plugin host under load WKWebView ran the editor before its stylesheet had
 * applied: the ramp read empty, the component refused to build its picture,
 * and every column after that went nowhere, behind a blank canvas, for the
 * whole session. Chrome does not lose that race by itself, so it is staged:
 * until the window's load event, no custom property has a value.
 */
test('the picture is built even when the stylesheet applies after the editor mounts',
  async ({ page, errors }) => {
    await page.addInitScript(() => {
      const real = globalThis.getComputedStyle;
      let styled = false;
      globalThis.addEventListener('load', () => { styled = true; }, { capture: true });
      globalThis.getComputedStyle = (el, pseudo) => {
        const style = real(el, pseudo);
        if (styled) return style;
        return new Proxy(style, {
          get: (target, key) => (key === 'getPropertyValue'
            ? (name) => (String(name).startsWith('--') ? '' : target.getPropertyValue(name))
            : Reflect.get(target, key)),
        });
      };
    });
    await open(page, 'spectrogram');
    const colours = () => page.evaluate(() => {
      const c = document.querySelector('.spectro-canvas');
      const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
      const seen = new Set();
      for (let i = 0; i < d.length; i += 4) seen.add((d[i] << 16) | (d[i + 1] << 8) | d[i + 2]);
      return seen.size;
    });
    await expect.poll(colours, { timeout: 5000 }).toBeGreaterThan(8);
    expect(errors).toEqual([]);
  });

test('the saved session is applied, and nothing is pushed before it', async ({ page }) => {
  await open(page, 'spectrogram', '?holdstate');
  await expect(page.locator('.hint-bar')).toContainText('19.7 kHz');
  /* A change before the session has arrived is not sent -- it would replace
   * the session with this editor's defaults. */
  await clash(page).click();
  await expect(clash(page)).toHaveAttribute('aria-checked', 'true');
  expect(await pushes(page)).toEqual([]);

  /* The session says clash is off: it wins over the early local change. */
  await page.evaluate(() => window.__releaseState());
  await expect(clash(page)).toHaveAttribute('aria-checked', 'false');
  expect(await pushes(page)).toEqual([]);

  await clash(page).click();
  expect(await pushes(page)).toEqual([
    `${MSG.clash} -60:12`,
    `${MSG.select} 1`,
    `${MSG.view} 0`,
    `${MSG.compare} 0:1:1`,
  ]);
});

test('a reopened zoom is shown as its named range, and not sent back', async ({ page }) => {
  await open(page, 'spectrogram', '?zoom=40.00:800.00');
  await expect(page.getByRole('combobox', { name: 'Range' })).toHaveValue('2');
  await expect(page.locator('.select-value').first()).toHaveText('Bass');
  expect(await pushes(page)).toEqual([]);
});

test('view, compare and range changes each push the session', async ({ page }) => {
  await open(page, 'spectrogram');
  await clearSent(page);

  /* VIEW: add Bass to the picture. The panel renders its rows on open. */
  await page.locator('.checklist-face').click();
  const rows = page.locator('.checklist-row [role="switch"]');
  await expect(rows).toHaveCount(3);
  await rows.nth(1).click();
  expect(await pushes(page)).toEqual([
    `${MSG.select} 1`, `${MSG.view} 0,1`, `${MSG.compare} 0:1:0`,
  ]);
  await expect(page.locator('.checklist-face .select-value')).toHaveText('input, Bass');
  await page.locator('.checklist-face').click();

  /* COMPARE: input against Pad. With the clash off, no bus is opened for it. */
  await clearSent(page);
  await page.getByRole('combobox', { name: 'Against' }).selectOption({ value: '2' });
  expect(await pushes(page)).toEqual([
    `${MSG.select} 1`, `${MSG.view} 0,1`, `${MSG.compare} 0:2:0`,
  ]);

  /* RANGE: Mid is 200 Hz to 4 kHz, and the mock sends the new axis back. */
  await clearSent(page);
  await page.getByRole('combobox', { name: 'Range' }).selectOption({ value: '3' });
  expect(await pushes(page)).toEqual([`${MSG.range} 200:4000`]);
  await expect(page.locator('.hint-bar')).toContainText('201 Hz – 4.0 kHz');
});

test('pause holds the picture and says so on its button', async ({ page }) => {
  await open(page, 'spectrogram');
  const pause = page.getByRole('button', { name: 'Pause the picture' });
  await pause.click();
  await expect(page.getByRole('button', { name: 'Resume the picture' }))
    .toHaveAttribute('aria-pressed', 'true');
  /* Pause never leaves the editor. */
  expect(await pushes(page)).toEqual([]);
});

test('the bar view follows the host tempo, and the crosshair reads the picture', async ({ page }) => {
  await open(page, 'spectrogram');
  const hint = page.locator('.hint-bar');
  await expect(hint).toContainText('history13 s');

  /* The mock's transport is 120 BPM in 4/4, running. */
  await page.getByRole('switch', { name: 'bars' }).click();
  await expect(hint).toContainText('4 bars · 120 BPM');
  await page.getByRole('combobox', { name: 'Bars' }).selectOption({ value: '0' });
  await expect(hint).toContainText('1 bar · 120 BPM');
  /* A view setting: the plugin is not told. */
  expect(await pushes(page)).toEqual([]);

  /* Over the picture the three readouts say where the pointer is -- in the
   * bar view the time is a position, "bar:beat" -- and away from it, "—". */
  const values = page.locator('.xh-val');
  const box = await page.locator('.spectro').boundingBox();
  await page.mouse.move(box.x + box.width * 0.5, box.y + box.height * 0.5);
  await expect(values.nth(0)).toHaveText(/^\d+(\.\d)? k?Hz$/);
  await expect(page.locator('.xh-key').nth(1)).toHaveText('pos');
  await expect(values.nth(1)).toHaveText(/^\d+:\d\.\d$/);
  await expect(values.nth(2)).toHaveText(/dB$/);
  await page.mouse.move(box.x + box.width * 0.5, box.y + box.height + 60);
  await expect(values.nth(0)).toHaveText('—');
});

test('the height is re-sent after a viewport resize', async ({ page }) => {
  await open(page, 'spectrogram');
  await expect.poll(() => texts(page, SHELL.height)).toEqual([String(H)]);
  await page.setViewportSize({ width: W * 0.75, height: H });
  await expect.poll(() => texts(page, SHELL.height))
    .toEqual([String(H), String(Math.ceil(H * 0.75))]);
});

test('looks as designed in its default state', async ({ page }) => {
  await openForScreenshot(page, 'spectrogram', 'spectrogram');
  /*
   * THE PICTURE ITSELF IS MASKED, and only the picture. The mock's column feed
   * starts with its script, while the page is still loading -- and Playwright's
   * paused clock only takes hold once the document has, so a column or two can
   * land before the editor is listening and be dropped. 2 runs in 30 drew the
   * sweep one column over. Everything around it -- the axes, the toolbar, the
   * strip, the hint bar -- is compared; what the picture draws from its bytes
   * is ramp.test.mjs's and columns.test.mjs's to pin.
   */
  await expect(page).toHaveScreenshot('spectrogram.png', {
    mask: [page.locator('.spectro-canvas')],
  });
});
