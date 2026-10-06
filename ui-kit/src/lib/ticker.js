/*
 * A steady clock for the animated ground, in a page its host shows as hidden.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * WHY NOT requestAnimationFrame. A plugin editor is a WKWebView inside the
 * host's process, and WebKit reports that page as HIDDEN in a real host --
 * document.hidden true, about one animation frame in three seconds -- while
 * the plugin's messages still arrive at once. A field clocked by
 * requestAnimationFrame, or one that paused while hidden, therefore never
 * moved in Live, which is the owner's report.
 *
 * WHY NOT ONLY A PAGE TIMER either. In that same hidden page WebKit throttles
 * the page's own timers too: measured in a real AU editor (tests/au_ground.m),
 * a 33 ms setInterval fired about three times a second. A dedicated worker's
 * timers are not throttled, and its messages reach the page at once: the same
 * 33 ms interval, in a worker, arrived about 28 times a second.
 *
 * SO THE CLOCK IS BOTH: the page's interval, and a worker's that posts to the
 * page on the same period. `fn` runs on either. A consumer that measures the
 * time since its last call -- as the field does -- is unaffected by the two
 * overlapping; it simply gets the steadier of the two. The page's interval is
 * also what a test's fake clock drives, so tests stay deterministic, and it is
 * the whole clock where there is no Worker (node, or a page whose policy
 * refuses a blob: worker).
 */

/* The worker: a number starts an interval of that many ms, 0 stops it. */
const WORKER_SOURCE =
  'let t = 0; onmessage = (e) => { clearInterval(t); ' +
  't = e.data > 0 ? setInterval(() => postMessage(0), e.data) : 0; };';

/** A worker running WORKER_SOURCE, or null where one cannot be made. */
function makeWorker() {
  const { Worker, Blob, URL: Url } = globalThis;
  if (typeof Worker !== 'function' || typeof Blob !== 'function' || !Url?.createObjectURL) {
    return null;
  }
  let url = null;
  try {
    url = Url.createObjectURL(new Blob([WORKER_SOURCE], { type: 'text/javascript' }));
    return { worker: new Worker(url), url };
  } catch {
    if (url) Url.revokeObjectURL?.(url);
    return null;
  }
}

/**
 * Call `fn` every `ms` while started.
 *
 * @returns { start(), stop(), destroy(), running }
 */
export function createTicker(fn, ms) {
  let running = false;
  let timer = 0;
  const made = makeWorker();
  if (made) {
    /* A message posted before a stop can still arrive after it. */
    made.worker.onmessage = () => { if (running) fn(); };
  }

  const ticker = {
    get running() { return running; },
    start() {
      if (running) return;
      running = true;
      timer = globalThis.setInterval(fn, ms);
      made?.worker.postMessage(ms);
    },
    stop() {
      if (!running) return;
      running = false;
      globalThis.clearInterval(timer);
      timer = 0;
      made?.worker.postMessage(0);
    },
    destroy() {
      ticker.stop();
      if (made) {
        made.worker.terminate();
        globalThis.URL.revokeObjectURL?.(made.url);
      }
    },
  };
  return ticker;
}
