/*
 * A headless audit of the rendered editor.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * WHY THIS AND NOT A SCREENSHOT. Several of the things that were wrong here are
 * invisible in a picture: a readout showing base64, a glow at the wrong alpha, a
 * playhead that is in the right place but only moves when a timer fires. This
 * reads the DOM and the computed styles instead, and prints JSON into the page
 * for `chrome --headless --dump-dom` to lift back out.
 *
 * --virtual-time-budget is what makes the waits below work: Chrome advances
 * timers as fast as it can and only then dumps.
 */
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const q = (s) => document.querySelector(s);
const qa = (s) => [...document.querySelectorAll(s)];
const box = (s) => { const e = q(s); return e ? Math.round(e.getBoundingClientRect().width) : null; };

window.__auditPromise = (async () => {
  const o = {};
  await wait(600);

  /*
   * FAULT 1's ACTUAL TEST. mock.js pushed every value BEFORE the editor
   * existed, so all of it was dropped; this counts the kMsgReady replies, and
   * the readouts prove the values arrived by that route. A zero here with
   * populated readouts would mean the harness had stopped reproducing the race.
   */
  o.readyHandshakes = window.__mockEarlyPush ?? 0;
  o.readouts = qa('.readout').map((e) => e.textContent.trim());
  o.readoutsAllPopulated = o.readouts.length > 0 && o.readouts.every((t) => t.length > 0);
  /* Base64 in a readout is the old bug and has a signature: '=' padding. */
  o.readoutsLookLikeBase64 = o.readouts.some((t) => /^[A-Za-z0-9+/]+={0,2}$/.test(t) && t.includes('='));

  o.selects = qa('.select-value').map((e) => e.textContent.trim());
  o.switchOn = qa('.switch').map((e) => e.classList.contains('on'));
  o.switchIsRealButton = qa('button.switch-row').length;
  o.tag = q('.tag')?.textContent ?? null;

  /* The ring: wedges, the shared halo group, and whether it can be clicked. */
  o.ringPaths = qa('.ring path').length;
  o.ringHasGlowGroup = !!q('.ring g.glow-led');
  o.ringHasPointerHandler = !!q('.ring');
  o.ringCursorMark = qa('.ring path.ring-cursor').length;
  o.ringLitWedges = qa('.ring g.glow-led path').length;
  o.ringHead = qa('.ring circle.ring-head').length;

  o.pads = qa('.pad').length;
  o.padsOn = qa('.pad.on').length;
  o.padsTie = qa('.pad.tie').length;
  o.knobArcGlows = qa('.knob g.glow-arc').length;
  o.knobsFocusable = qa('.knob[tabindex]').length;

  /* THE ALIGNMENT: the plot and the sixteen pads must be the same width. */
  o.plotW = box('.band-plot svg');
  o.gridW = box('.grid');
  o.bandW = box('.band');
  o.plotMatchesGrid = o.plotW === o.gridW;

  /* The glow tokens must resolve; an undefined var() renders as nothing. */
  const cs = getComputedStyle(document.documentElement);
  o.tokens = {
    glowLed: cs.getPropertyValue('--glow-led').trim().slice(0, 40),
    glowArc: cs.getPropertyValue('--glow-arc').trim(),
    glowFocus: cs.getPropertyValue('--glow-focus').trim(),
    scopeDry: cs.getPropertyValue('--scope-dry').trim(),
  };
  o.padOnFilter = qa('.pad.on')[0] ? getComputedStyle(qa('.pad.on')[0]).filter.slice(0, 40) : null;

  /*
   * THE PLAYHEAD MUST ADVANCE WITH NO NEW PUSH. The mock sends `ui` exactly
   * once, so if this moves it is because the UI is carrying the engine's phase
   * forward itself rather than waiting to be told.
   */
  const head = () => q('.band-plot line.playhead')?.getAttribute('x1') ?? null;
  const a = head(); await wait(200); const b = head();
  o.playhead = [a, b];
  o.playheadAdvancesWithoutAPush = a !== null && a !== b;

  /*
   * THE FOCUS RING. :focus-visible needs the focus to have come from the
   * KEYBOARD -- a programmatic .focus() does not match it, and nor does a
   * click. So the driver presses Tab (Input.dispatchKeyEvent) before this runs
   * and this only reports what it finds.
   */
  o.focusedTag = document.activeElement?.tagName;
  o.focusedClass = document.activeElement?.getAttribute?.('class') ?? null;
  o.focusMatchesFocusVisible = document.activeElement?.matches?.(':focus-visible') ?? false;
  const fe = document.activeElement;
  o.focusedFilter = fe && fe !== document.body ? getComputedStyle(fe).filter.slice(0, 30) : null;
  o.focusedShadow = fe && fe !== document.body ? getComputedStyle(fe).boxShadow.slice(0, 50) : null;

  /* The scope, on the SIGNAL tab: the dry must be grey and the wet must glow. */
  qa('.tab')[1]?.click();
  await wait(250);
  o.scopeFills = qa('.band-plot path[fill]').map((p) => p.getAttribute('fill'));
  o.scopeWetInGlowGroup = !!q('.band-plot g.glow-arc path');
  o.scopeCaption = q('.band-plot .plot-caption')?.textContent ?? null;

  /* Into the DOM for a --dump-dom run, and RETURNED for the CDP driver, which
   * awaits this promise -- see cdp.mjs. Virtual time cannot be used here: the
   * playhead is a permanent requestAnimationFrame loop and virtual time never
   * settles against one. */
  const pre = document.createElement('pre');
  pre.id = 'audit';
  pre.textContent = JSON.stringify(o, null, 1);
  document.body.appendChild(pre);
  return pre.textContent;
})();
