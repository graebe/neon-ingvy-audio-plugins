/*
 * The Trance Gate's editor, end to end against its mock host.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The mock's pattern is the 5555 mask -- every other step on, steps 2 and 6
 * tied, step 4 at half amount -- so each gesture below starts from a known
 * step. Every assertion is on what the editor TOLD THE PLUGIN, decoded from the
 * mock's record, or on what the page shows.
 */
import {
  test, expect, open, openForScreenshot, sent, texts, writes, clearSent,
  pushParam, dragVertically, SHELL,
} from './harness.mjs';

const W = 824, H = 752;
/* lib/msg.js */
const MSG = { exportFile: 107, importFile: 108, patch: 67, setStep: 96, setDepth: 97, requestPatch: 99, setOrder: 103, randomize: 104 };
const P = { slot: 0, length: 1, rate: 2, timeMode: 4, amount: 6, width: 7, attack: 8 };

test.use({ viewport: { width: W, height: H } });

const pad = (page, i) => page.locator(`.grid [data-step="${i}"]`);

test('loads without a console error and completes the ready handshake', async ({ page, errors }) => {
  await open(page, 'trance-gate');
  /* Values, texts and the pattern all arrive in the reply to `ready`. */
  await expect(page.getByRole('slider', { name: 'Width' })).toHaveAttribute('aria-valuenow', '0.75');
  await expect(page.getByRole('slider', { name: 'Width' })).toHaveAttribute('aria-valuetext', '75.00 %');
  await expect(page.locator('svg.ring')).toHaveAttribute('aria-valuenow', '16');
  await expect(pad(page, 0)).toHaveAttribute('aria-label', 'Step 1, on, 100 %');
  await expect(pad(page, 1)).toHaveAttribute('aria-label', 'Step 2, off');
  await expect(pad(page, 2)).toHaveAttribute('aria-label', 'Step 3, tie, 100 %');
  expect((await sent(page)).filter((m) => m.msgTag === SHELL.ready)).toHaveLength(1);
  expect(errors).toEqual([]);
});

test('a double-click resets a knob to the default the plugin sent', async ({ page }) => {
  await open(page, 'trance-gate');
  const amount = page.getByRole('slider', { name: 'Amount' });
  await expect(amount).toHaveAttribute('aria-valuenow', '0.9');
  await clearSent(page);
  /* A real double-click: two presses and releases, then `dblclick`. */
  await amount.dblclick();
  /* The mock's defaults (tag 113) say Amount is 1 -- not 0, not the session's 0.9. */
  expect(await writes(page, P.amount)).toEqual([1]);
  await expect(amount).toHaveAttribute('aria-valuenow', '1');
  /* ONE edit, one undo step: the two presses themselves moved nothing, so
   * they told the host nothing -- no empty touch gestures around the reset. */
  expect((await sent(page)).filter((m) => m.paramIdx === P.amount).map((m) => m.msg))
    .toEqual(['BPCFUI', 'SPVFUI', 'EPCFUI']);
});

test('a knob drag is one gesture, and a host echo mid-drag does not move it', async ({ page }) => {
  await open(page, 'trance-gate');
  const width = page.getByRole('slider', { name: 'Width' });
  await clearSent(page);

  let during;
  await dragVertically(page, width, -20, {
    during: async () => {
      during = Number(await width.getAttribute('aria-valuenow'));
      /* The host echoing an OLDER value while the pointer is still down. */
      await pushParam(page, P.width, 0.1);
    },
  });
  const msgs = (await sent(page)).filter((m) => m.paramIdx === P.width);
  expect(msgs[0].msg).toBe('BPCFUI');
  expect(msgs.at(-1).msg).toBe('EPCFUI');
  const values = msgs.filter((m) => m.msg === 'SPVFUI').map((m) => m.value);
  expect(values.length).toBeGreaterThan(1);
  /* 20 px up of the knob's 200 px travel: 0.75 -> 0.85. */
  expect(values.at(-1)).toBeCloseTo(0.85, 5);
  expect(during).toBeGreaterThan(0.75);
  /* The echo was ignored: the knob sits where the drag left it. */
  expect(Number(await width.getAttribute('aria-valuenow'))).toBeCloseTo(0.85, 5);

  /* After the gesture the host is authoritative again. */
  await pushParam(page, P.width, 0.2);
  await expect(width).toHaveAttribute('aria-valuenow', '0.2');
});

test('the readout: one click edits, Enter commits via setText, Escape sends nothing', async ({ page }) => {
  await open(page, 'trance-gate');
  const readout = page.getByRole('button', { name: 'Width value' });
  const field = page.getByRole('textbox', { name: 'Width' });

  await clearSent(page);
  await readout.click();
  await expect(field).toBeFocused();
  await field.fill('40 %');
  await field.press('Escape');
  await expect(field).toHaveCount(0);
  expect(await texts(page, SHELL.setText)).toEqual([]);

  await readout.click();
  await field.fill('40 %');
  await field.press('Enter');
  await expect(field).toHaveCount(0);
  /* "<paramIdx>:<text>" -- the PLUGIN parses it, with the unit. */
  expect(await texts(page, SHELL.setText)).toEqual([`${P.width}:40 %`]);
});

test('the pads: click toggles, shift-click ties, a drag sets the amount', async ({ page }) => {
  await open(page, 'trance-gate');
  await clearSent(page);

  await pad(page, 1).click();                       /* off -> on, at full amount */
  await pad(page, 0).click({ modifiers: ['Shift'] });   /* on -> tie */
  await pad(page, 8).click();                       /* on -> off */
  expect(await texts(page, MSG.setStep)).toEqual(['1:1', '0:2', '8:0']);
  expect(await texts(page, MSG.setDepth)).toEqual(['1:1.0000']);

  /* A vertical drag inside a pad is its amount, measured against the pad. */
  await clearSent(page);
  const box = await pad(page, 3).boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height * 0.7, { steps: 6 });
  await page.mouse.up();
  const depths = await texts(page, MSG.setDepth);
  const [step, amount] = depths.at(-1).split(':');
  expect(step).toBe('3');
  expect(Number(amount)).toBeCloseTo(0.3, 1);
});

test('the pads from the keyboard: arrows move, Space toggles, Alt+Up raises the amount', async ({ page }) => {
  await open(page, 'trance-gate');
  await clearSent(page);
  await pad(page, 0).focus();
  await page.keyboard.press('ArrowRight');
  await expect(pad(page, 1)).toBeFocused();
  await page.keyboard.press('Space');
  await page.keyboard.press('ArrowDown');           /* one row of 16: nowhere to go */
  await expect(pad(page, 1)).toBeFocused();
  await page.keyboard.press('ArrowRight');
  await page.keyboard.press('ArrowRight');
  await page.keyboard.press('ArrowRight');
  await expect(pad(page, 4)).toBeFocused();
  /* Step 4 is at 0x80 of 255; Alt+Up is a tenth more. */
  await page.keyboard.press('Alt+ArrowUp');
  expect(await texts(page, MSG.setStep)).toEqual(['1:1']);
  expect(await texts(page, MSG.setDepth)).toEqual(['1:1.0000', '4:0.6020']);
});

test('the ring is the Length slider to the keyboard', async ({ page }) => {
  await open(page, 'trance-gate');
  await clearSent(page);
  const ring = page.locator('svg.ring');
  await ring.focus();
  await page.keyboard.press('ArrowUp');
  await page.keyboard.press('PageDown');
  await page.keyboard.press('PageUp');
  /* 17 steps; then (the mock does not echo) Page goes to the Length detents
   * at 1/16 -- 8, 16, 32, 64 -- so from 16, down to 8 and up to 32. */
  expect(await writes(page, P.length)).toEqual([16 / 127, 7 / 127, 31 / 127]);
});

/*
 * THE LENGTH DETENTS: half a bar to four bars at the Rate, as the engine lists
 * them on the `params` readout. The host moves the Rate and the Length here
 * (the mock's __hostMoves), as automation would.
 */
const hostMoves = (page, idx, value) =>
  page.evaluate(([i, v]) => window.__hostMoves(i, v), [idx, value]);
const steps = (n) => (n - 1) / 127;
const RATE_1_16 = 7 / 12, RATE_1_32 = 9 / 12;

test('at 1/32 a Length drag passing near 32 lands on it and holds', async ({ page }) => {
  await open(page, 'trance-gate');
  await hostMoves(page, P.rate, RATE_1_32);
  await hostMoves(page, P.length, steps(20));
  const length = page.locator('svg.knob[aria-label="Length"]');
  await expect(length.locator('.knob-tick')).toHaveCount(4);

  /* 32 is 12 steps -- 18.9px of the knob's 200 -- above 20. 24px up would be
   * 35 steps on a plain knob; it is inside 32's hold. */
  await clearSent(page);
  await dragVertically(page, length, -24, { steps: 12 });
  expect((await writes(page, P.length)).at(-1)).toBe(steps(32));
  await expect(length.locator('.knob-tick.on')).toHaveCount(1);

  /* 31px up is 39 steps on a plain knob, and still 32 here: the hold is 14px. */
  await hostMoves(page, P.length, steps(20));
  await clearSent(page);
  await dragVertically(page, length, -31, { steps: 16 });
  expect((await writes(page, P.length)).at(-1)).toBe(steps(32));

  /* Past the hold it moves on, and a fine (shift) drag ignores it. */
  await hostMoves(page, P.length, steps(20));
  await clearSent(page);
  await dragVertically(page, length, -40, { steps: 16 });
  expect(Math.round((await writes(page, P.length)).at(-1) * 127) + 1).toBeGreaterThan(32);
  await hostMoves(page, P.length, steps(31));
  await clearSent(page);
  await page.keyboard.down('Shift');
  await dragVertically(page, length, -8, { steps: 8 });
  await page.keyboard.up('Shift');
  const fine = (await writes(page, P.length)).at(-1);
  expect(fine).toBeGreaterThan(steps(31));
  expect(fine).not.toBe(steps(32));
});

test('Page Up and Down jump between the Length detents, which follow the Rate', async ({ page }) => {
  await open(page, 'trance-gate');
  await hostMoves(page, P.rate, RATE_1_32);
  await hostMoves(page, P.length, steps(20));
  const length = page.locator('svg.knob[aria-label="Length"]');
  await length.focus();
  await clearSent(page);
  await page.keyboard.press('PageUp');
  expect(await writes(page, P.length)).toEqual([steps(32)]);
  /* The arrows are untouched: the knob's 1 %, which the host's integer
   * Length rounds to the next step. */
  await hostMoves(page, P.length, steps(20));
  await clearSent(page);
  await page.keyboard.press('ArrowUp');
  const [arrow] = await writes(page, P.length);
  expect(arrow).toBeCloseTo(steps(20) + 0.01, 9);
  expect(Math.round(arrow * 127) + 1).toBe(21);

  /* At 1/16 the detents are 8, 16, 32, 64: from 14, down is 8 -- at 1/32
   * there was nothing below 16. The ticks moved with them. */
  const before = await length.locator('.knob-tick').evaluateAll((t) => t.map((e) => e.getAttribute('d')));
  await hostMoves(page, P.rate, RATE_1_16);
  await expect.poll(() => length.locator('.knob-tick').evaluateAll((t) => t.map((e) => e.getAttribute('d'))))
    .not.toEqual(before);
  await hostMoves(page, P.length, steps(14));
  await clearSent(page);
  await page.keyboard.press('PageDown');
  expect(await writes(page, P.length)).toEqual([steps(8)]);
  await hostMoves(page, P.length, steps(64));
  await expect(length.locator('.knob-tick.on')).toHaveCount(1);
});

test('only the Length knob has detents', async ({ page }) => {
  await open(page, 'trance-gate');
  await expect(page.locator('.knob-tick')).toHaveCount(4);
  await expect(page.locator('svg.knob[aria-label="Length"]').locator('.knob-tick')).toHaveCount(4);
});

test('copy and paste carry the pattern through the clipboard', async ({ page, context }) => {
  await context.grantPermissions(['clipboard-read', 'clipboard-write']);
  await open(page, 'trance-gate');
  await clearSent(page);
  await page.getByRole('button', { name: 'Copy gate config' }).click();
  expect(await texts(page, MSG.requestPatch)).toEqual(['']);
  /* The mock answers with the patch; the editor puts it on the clipboard. */
  await expect.poll(() => page.evaluate(() => navigator.clipboard.readText()))
    .toBe('tg1:slot=0:len=16:steps=5555');

  await page.getByRole('button', { name: 'Paste gate config' }).click();
  await expect.poll(() => texts(page, MSG.patch)).toEqual(['tg1:slot=0:len=16:steps=5555']);
});

test('paste falls back to a field when the clipboard cannot be read', async ({ page }) => {
  await open(page, 'trance-gate');
  await clearSent(page);
  await page.getByRole('button', { name: 'Paste gate config' }).click();
  const field = page.locator('.paste-field');
  await expect(field).toBeFocused();
  await field.evaluate((el) => {
    const data = new DataTransfer();
    data.setData('text', 'tg1:slot=2:len=8:steps=00FF');
    el.dispatchEvent(new ClipboardEvent('paste', { clipboardData: data, bubbles: true, cancelable: true }));
  });
  await expect(field).toHaveCount(0);
  expect(await texts(page, MSG.patch)).toEqual(['tg1:slot=2:len=8:steps=00FF']);
});

test('the stage readouts follow Env Time between ms and %', async ({ page }) => {
  await open(page, 'trance-gate');
  const attack = page.getByRole('button', { name: 'Attack value' });
  await expect(attack.locator('.num')).toHaveText('1.5');
  await expect(attack.locator('.unit')).toHaveText('ms');

  await page.getByRole('combobox', { name: 'Time' }).selectOption({ value: '1' });
  expect(await writes(page, P.timeMode)).toEqual([1]);
  await expect(attack.locator('.num')).toHaveText('1.60');
  await expect(attack.locator('.unit')).toHaveText('%');

  await page.getByRole('combobox', { name: 'Time' }).selectOption({ value: '0' });
  await expect(attack.locator('.unit')).toHaveText('ms');
});

test('switching the slot shows every control at that slot\'s values', async ({ page }) => {
  await open(page, 'trance-gate');
  const amount = page.getByRole('slider', { name: 'Amount' });
  const width = page.getByRole('slider', { name: 'Width' });
  const attack = page.getByRole('button', { name: 'Attack value' });
  const time = page.getByRole('combobox', { name: 'Time' });
  await expect(amount).toHaveAttribute('aria-valuenow', '0.9');
  await time.selectOption({ value: '1' });           /* slot 1 reads in % */
  await expect(attack.locator('.unit')).toHaveText('%');
  await clearSent(page);

  /* Slot 2 has never been touched: every value is the plugin's default. */
  await page.getByRole('combobox', { name: 'Slot' }).selectOption({ value: '1' });
  await expect(amount).toHaveAttribute('aria-valuenow', '1');
  await expect(amount).toHaveAttribute('aria-valuetext', '100.00 %');
  await expect(width).toHaveAttribute('aria-valuenow', '1');
  await expect(time).toHaveValue('0');
  await expect(attack.locator('.num')).toHaveText('2.0');
  await expect(attack.locator('.unit')).toHaveText('ms');

  /* And back: slot 1 as it was left, Env Time included. */
  await page.getByRole('combobox', { name: 'Slot' }).selectOption({ value: '0' });
  await expect(amount).toHaveAttribute('aria-valuenow', '0.9');
  await expect(width).toHaveAttribute('aria-valuenow', '0.75');
  await expect(time).toHaveValue('1');
  await expect(attack.locator('.unit')).toHaveText('%');

  /* The recall is the plugin's: the editor wrote the Slot and nothing else,
   * so no echo went back as an edit. */
  const edited = (await sent(page)).filter((m) => m.msg === 'SPVFUI').map((m) => m.paramIdx);
  expect(new Set(edited)).toEqual(new Set([P.slot]));
});

/* A slot file's outcome: the first clause of the hint bar, verb then the rest. */
const expectStatus = async (page, verb, rest) => {
  await expect(page.locator('.hint-bar .hint-key').first()).toHaveText(verb);
  await expect(page.locator('.hint-bar .hint-val').first()).toHaveText(rest);
};

test('EXPORT and EXPORT ALL ask the plugin for a file, and the hint says how it went', async ({ page }) => {
  await open(page, 'trance-gate');
  await clearSent(page);
  await page.getByRole('button', { name: 'EXPORT', exact: true }).click();
  await expectStatus(page, 'Exported', 'slot 1 to NI Trance Gate Slot 1.nitgslot.');
  /* From the keyboard too: a button is focusable and Enter presses it. */
  await page.getByRole('button', { name: 'EXPORT ALL', exact: true }).focus();
  await page.keyboard.press('Enter');
  await expectStatus(page, 'Exported', 'all 8 slots to NI Trance Gate Bank.nitgbank.');
  expect(await texts(page, MSG.exportFile)).toEqual(['slot', 'bank']);
});

test('IMPORT of a slot file replaces the current slot and the controls follow', async ({ page }) => {
  await open(page, 'trance-gate');
  const amount = page.getByRole('slider', { name: 'Amount' });
  await page.getByRole('combobox', { name: 'Slot' }).selectOption({ value: '2' });
  await expect(amount).toHaveAttribute('aria-valuenow', '1');
  await page.getByRole('button', { name: 'IMPORT', exact: true }).click();
  expect(await texts(page, MSG.importFile)).toEqual(['']);
  /* fixtures/slot.nitgslot: Amount 70 %, Rate 1/8, Env Time %, S-Curve. */
  await expect(amount).toHaveAttribute('aria-valuenow', '0.7');
  await expect(page.getByRole('combobox', { name: 'Time' })).toHaveValue('1');
  await expect(page.getByRole('combobox', { name: 'Curve' })).toHaveValue('2');
  await expectStatus(page, 'Imported', 'slot.nitgslot into slot 3.');
  /* Slot 1 is as it was. */
  await page.getByRole('combobox', { name: 'Slot' }).selectOption({ value: '0' });
  await expect(amount).toHaveAttribute('aria-valuenow', '0.9');
});

test('IMPORT of a bank replaces all eight slots', async ({ page }) => {
  await open(page, 'trance-gate');
  await page.evaluate(() => { window.__importFixture = 'bank'; });
  const amount = page.getByRole('slider', { name: 'Amount' });
  await page.getByRole('button', { name: 'IMPORT', exact: true }).click();
  await expectStatus(page, 'Imported', 'all 8 slots from bank.nitgbank.');
  await expect(amount).toHaveAttribute('aria-valuenow', '1');   /* the bank's slot 1 */
  await page.getByRole('combobox', { name: 'Slot' }).selectOption({ value: '1' });
  await expect(amount).toHaveAttribute('aria-valuenow', '0.4');
  await expect(page.getByRole('slider', { name: 'Width' })).toHaveAttribute('aria-valuenow', '0.5');
});

test('the band switches between the pattern and the live signal', async ({ page }) => {
  await open(page, 'trance-gate');
  const band = page.locator('.band');
  const signal = page.getByRole('tab', { name: 'Signal' });
  await expect(band).toContainText('PATTERN');
  await signal.click();
  await expect(signal).toHaveAttribute('aria-selected', 'true');
  /* The mock's scope is a 2000 ms cycle, and the caption says so. */
  await expect(band).toContainText('SIGNAL ONE CYCLE, 2000 MS');
  /* And back from the keyboard: the strip is one tab stop, arrows move. */
  await signal.focus();
  await page.keyboard.press('ArrowUp');
  await expect(page.getByRole('tab', { name: 'Pattern' })).toHaveAttribute('aria-selected', 'true');
  await expect(band).toContainText('PATTERN');
});

test('ORDER mode names the arrivals by tapping, and SHUFFLE deals a new order', async ({ page }) => {
  await open(page, 'trance-gate');
  await clearSent(page);
  const order = page.getByRole('button', { name: /^ORDER/ });
  await order.click();
  /* Eight steps are on in the 5555 mask: that is what can be sequenced. */
  await expect(order).toHaveText('ORDER 0/8');
  await pad(page, 6).click();
  await pad(page, 0).click();
  await pad(page, 1).click();                 /* off: not an arrival, ignored */
  await expect(order).toHaveText('ORDER 2/8');
  /* A tap names; it does not toggle. */
  expect(await texts(page, MSG.setOrder)).toEqual(['6:1', '0:2']);
  expect(await texts(page, MSG.setStep)).toEqual([]);
  await order.click();
  await expect(order).toHaveText('ORDER');

  await clearSent(page);
  await page.getByRole('button', { name: 'SHUFFLE' }).click();
  /* One rank per arriving step, 1..8 each once, however they were dealt. */
  const dealt = (await texts(page, MSG.setOrder)).map((t) => t.split(':').map(Number));
  expect(dealt.map(([step]) => step).sort((a, b) => a - b)).toEqual([0, 2, 4, 6, 8, 10, 12, 14]);
  expect(dealt.map(([, rank]) => rank)).toEqual([1, 2, 3, 4, 5, 6, 7, 8]);
});

test('RANDOM asks the engine to reroll the slot', async ({ page }) => {
  await open(page, 'trance-gate');
  await clearSent(page);
  await page.getByRole('button', { name: 'RANDOM' }).click();
  /* No payload: the engine walks its own generator, so two presses differ. */
  expect(await texts(page, MSG.randomize)).toEqual(['']);
});

test('the height is re-sent after a viewport resize', async ({ page }) => {
  await open(page, 'trance-gate');
  await expect.poll(() => texts(page, SHELL.height)).toEqual([String(H)]);
  await page.setViewportSize({ width: W * 0.75, height: H });
  await expect.poll(() => texts(page, SHELL.height))
    .toEqual([String(H), String(Math.ceil(H * 0.75))]);
});

test('looks as designed in its default state', async ({ page }) => {
  await openForScreenshot(page, 'trance-gate', 'trance-gate');
  await expect(page).toHaveScreenshot('trance-gate.png');
});
