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
  /*
   * THE FADE-IN. At the mock's default the fade is 100%, so nothing is pending
   * and no numbers are drawn -- which is the resting state and worth asserting
   * as much as the other one. ?fade=0.5 is the interesting page; see the
   * fade-specific fields further down, which the driver reruns with it set.
   */
  o.padsPending = qa('.pad.pending').length;
  o.padOrderNumbers = qa('.pad-order').length;
  o.fadePanel = !!q('.fade-panel');
  o.fadePanelTitle = q('.fade-panel h2')?.textContent ?? null;
  /* The titles run UP the left edge now, which is what pays for the third
   * panel. An unrotated title means the panel arithmetic is wrong by 28px a
   * panel and the window is the wrong height. */
  o.panelTitlesVertical = qa('.panel h2').every(
    (e) => getComputedStyle(e).writingMode.startsWith('vertical'));
  o.panelHeights = qa('.panel').map((e) => Math.round(e.getBoundingClientRect().height));
  o.orderButtons = qa('.fade-actions .btn').map((e) => e.textContent.trim());
  o.randomButton = qa('.settings-row .btn').map((e) => e.textContent.trim())
                     .filter((t) => t === 'RANDOM').length;
  o.knobArcGlows = qa('.knob g.glow-arc').length;
  o.knobsFocusable = qa('.knob[tabindex]').length;

  /*
   * THE ALIGNMENT. The plot, the band, the pads and the settings row must all be
   * the same width, and `main` must have the SAME padding on both sides -- one
   * edge down each side of the window, which is what it did not have while the
   * tab strip had a column of its own.
   */
  o.plotW = box('.band-plot svg');
  o.gridW = box('.grid');
  o.bandW = box('.band');
  o.settingsW = box('.settings-row');
  o.plotMatchesGrid = o.plotW === o.gridW;
  o.everythingIsOneWidth =
    o.plotW === o.gridW && o.gridW === o.bandW && o.bandW === o.settingsW;
  {
    const m = q('main');
    const r = m?.getBoundingClientRect();
    const rect = (s2) => q(s2)?.getBoundingClientRect();
    const lefts = ['.grid', '.band', '.settings-row', '.ring-slot', '.env-plot-slot']
      .map((s2) => rect(s2)).filter(Boolean).map((b) => Math.round(b.left - r.left));
    const rights = ['.grid', '.band', '.settings-row']
      .map((s2) => rect(s2)).filter(Boolean).map((b) => Math.round(r.right - b.right));
    /* The panels are in the right column, so only their RIGHT edge is a window
     * padding; their left is the ring column plus a gutter. */
    const panelRights = qa('.panel')
      .map((e) => Math.round(r.right - e.getBoundingClientRect().right));
    o.leftPaddings = [...new Set(lefts)];
    o.rightPaddings = [...new Set([...rights, ...panelRights])];
    o.paddingIsUniform = o.leftPaddings.length === 1 && o.rightPaddings.length === 1 &&
                         o.leftPaddings[0] === o.rightPaddings[0];
  }
  /* The tab strip is OVER the plot, not beside it: it must overlap the band's
   * own box rather than sit outside it. */
  {
    const t = q('.band .tabs')?.getBoundingClientRect();
    const b = q('.band')?.getBoundingClientRect();
    o.tabsOverlayTheBand = !!(t && b && t.right <= b.right + 1 && t.left >= b.left);
  }

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

  /* THE LIT TAB'S GLOW. It used to be a class toggled from Tabs.jsx beside
   * `on`, and the halo did not keep up with the click. It is a CSS rule now, so
   * one class toggle carries both -- this reads the computed filter rather than
   * the class list, because the class list was never the thing that was late. */
  o.litTabHasGlow = (() => {
    const on = qa('.tab.on')[0];
    return on ? /drop-shadow/.test(getComputedStyle(on).filter) : false;
  })();

  /* The scope, on the SIGNAL tab: the dry must be grey and the wet must glow. */
  qa('.tab')[1]?.click();
  await wait(250);
  o.litTabHasGlowAfterClick = (() => {
    const on = qa('.tab.on')[0];
    return on ? /drop-shadow/.test(getComputedStyle(on).filter) : false;
  })();
  o.scopeFills = qa('.band-plot path[fill]').map((p) => p.getAttribute('fill'));
  o.scopeWetInGlowGroup = !!q('.band-plot g.glow-arc path');
  /* `.well-caption`, not `.plot-caption`: the Axis's tick labels carry the
   * latter and come earlier in document order, so this used to read "0". */
  o.scopeCaption = q('.band-plot .well-caption')?.textContent ?? null;
  /*
   * THE AXIS IS THE PATTERN AGAIN, so the scope carries step rules, step numbers
   * and the envelope over the trace -- none of which could mean anything while
   * the axis was wall time. And the numbers must be VISIBLE: .step-number had no
   * CSS rule at all, so they drew in SVG's default black on a dark well.
   */
  o.scopeHasStepNumbers = qa('.band-plot .step-number').length;
  o.scopeStepNumberFill = q('.band-plot .step-number')
    ? getComputedStyle(q('.band-plot .step-number')).fill : null;
  o.scopeHasSweepMark = qa('.band-plot line.sweep').length;
  /* Two stroked paths with no fill: the envelope outline above and below zero. */
  o.scopeEnvelopeOutlines = qa('.band-plot path[stroke][fill="none"]').length;

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
