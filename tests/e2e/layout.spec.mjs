/*
 * The window stays a window: in every editor, nothing scrolls it and nothing
 * leaves it. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * WHY THIS IS A TEST OF ITS OWN. In Live, pressing the Trance Gate's paste
 * opened a field that ran past the window's right edge and took the focus; the
 * browser scrolled it into view, and the whole editor slid left with the ring
 * and EXPORT cut off. A plugin window has no scrollbar to put that right. The
 * frame now refuses to scroll at all (EditorFrame, tokens.css), and this holds
 * every editor to it: at rest, with each control focused in turn -- focus is
 * what scrolls -- and, in the Trance Gate, through copy, paste, export and
 * import, which are the actions that put something new in the window.
 */
import { test, expect, open, layoutBreaches, settle } from './harness.mjs';

const EDITORS = [
  { plugin: 'trance-gate', W: 824, H: 752 },
  { plugin: 'side-chain', W: 760, H: 604 },
  { plugin: 'spectrogram', W: 720, H: 502 },
  { plugin: 'listen-in', W: 360, H: 232 },
];

for (const { plugin, W, H } of EDITORS) {
  test.describe(plugin, () => {
    test.use({ viewport: { width: W, height: H } });

    test('focusing any control scrolls nothing, and nothing leaves the window', async ({ page }) => {
      await open(page, plugin);
      await settle(page);
      expect(await layoutBreaches(page)).toEqual([]);
      const focusable = page.locator('main [tabindex="0"], main button, main input, main select, main [role="slider"]');
      const n = await focusable.count();
      expect(n).toBeGreaterThan(0);
      for (let i = 0; i < n; i++) {
        await focusable.nth(i).focus();
        await settle(page);
        expect(await layoutBreaches(page), `focused #${i}`).toEqual([]);
      }
      /* The one control that opens something: a CheckList's panel opens inside
       * the window, never past it (the Spectrogram's channels). */
      const lists = page.locator('main .checklist-face');
      for (let i = 0; i < await lists.count(); i++) {
        await lists.nth(i).click();
        await settle(page);
        expect(await layoutBreaches(page), `checklist #${i} open`).toEqual([]);
        await page.keyboard.press('Escape');
      }
    });

    /* THE FRAME'S OWN GUARANTEE, whatever an editor lays out: a control past
     * the right or the bottom edge is clipped, and focusing it -- which is what
     * scrolled Live's Trance Gate sideways -- moves nothing. */
    test('a control past the edge is clipped, and focusing it scrolls nothing', async ({ page }) => {
      await open(page, plugin);
      const scrolled = await page.evaluate(async ([w, h]) => {
        const out = [];
        for (const [left, top] of [[w + 40, 40], [40, h + 40], [w + 40, h + 40]]) {
          const b = document.createElement('button');
          b.textContent = 'stray';
          b.style.cssText = `position:absolute;left:${left}px;top:${top}px;width:120px;height:28px`;
          document.querySelector('main').appendChild(b);
          b.focus();
          b.scrollIntoView();
          await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));
          for (const el of [document.scrollingElement, document.body, document.querySelector('main')])
            if (el.scrollLeft || el.scrollTop) out.push(`${el.tagName} ${el.scrollLeft},${el.scrollTop}`);
          b.remove();
        }
        return out;
      }, [W, H]);
      expect(scrolled).toEqual([]);
    });
  });
}

test.describe('trance-gate actions', () => {
  test.use({ viewport: { width: 824, height: 752 } });

  /* The hint bar's first clause, once an action has said how it went. */
  const said = (page, verb) => expect(page.locator('.hint-bar .hint-key').first()).toHaveText(verb);

  test('copy, paste, export and import scroll nothing and stay inside the window', async ({ page }) => {
    await open(page, 'trance-gate');
    /* A refused paste first: its reason is the longest words the bar shows. */
    await page.evaluate(() => { window.__clipboard = 'not a slot'; });
    const steps = [
      [/^Paste/, 'Failed'],
      [/^Copy/, 'Copied'],
      [/^Paste/, 'Pasted'],
      ['EXPORT', 'Exported'],
      ['EXPORT ALL', 'Exported'],
      ['IMPORT', 'Imported'],
    ];
    for (const [name, verb] of steps) {
      await page.getByRole('button', { name, exact: typeof name === 'string' }).click();
      await settle(page);
      expect(await layoutBreaches(page), `during ${name}`).toEqual([]);
      await said(page, verb);
      await settle(page);
      expect(await layoutBreaches(page), `after ${name}`).toEqual([]);
    }
  });
});
