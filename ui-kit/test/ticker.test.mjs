/*
 * The ground's frame clock: a page timer and a worker's, either one enough.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * lib/ticker.js says why it is two clocks: a plugin host shows the editor as
 * hidden, WebKit throttles a hidden page's own timers to a few hertz, and a
 * dedicated worker's are not throttled. These hold the mechanics -- both start
 * and stop together, a late worker message after a stop does nothing, the
 * page timer alone is the clock where no worker can be made -- with the
 * Worker, Blob and URL globals stood in for, since node has none of them.
 */
import { test } from 'node:test';
import assert from 'node:assert/strict';

import { createTicker } from '../src/lib/ticker.js';

/** Stand-ins for the globals the ticker touches; returns them and a teardown. */
function install({ worker = true, workerThrows = false } = {}) {
  const keys = ['setInterval', 'clearInterval', 'Worker', 'Blob', 'URL'];
  const saved = Object.fromEntries(keys.map((k) => [k, globalThis[k]]));
  const env = { intervals: new Map(), workers: [], revoked: [], next: 0 };
  globalThis.setInterval = (fn, ms) => { env.intervals.set(++env.next, { fn, ms }); return env.next; };
  globalThis.clearInterval = (id) => { env.intervals.delete(id); };
  if (worker) {
    globalThis.Blob = class { constructor(parts, opts) { this.parts = parts; this.opts = opts; } };
    globalThis.URL = {
      createObjectURL: (blob) => { env.blob = blob; return 'blob:ticker'; },
      revokeObjectURL: (url) => { env.revoked.push(url); },
    };
    globalThis.Worker = class {
      constructor(url) {
        if (workerThrows) throw new Error('refused by policy');
        this.url = url;
        this.posted = [];
        this.terminated = false;
        env.workers.push(this);
      }
      postMessage(m) { this.posted.push(m); }
      terminate() { this.terminated = true; }
    };
  } else {
    delete globalThis.Worker;
  }
  const teardown = () => {
    for (const k of keys) {
      if (saved[k] === undefined) delete globalThis[k]; else globalThis[k] = saved[k];
    }
  };
  return { env, teardown };
}

test('start runs the page timer and the worker together, stop stops both', () => {
  const { env, teardown } = install();
  try {
    let n = 0;
    const t = createTicker(() => n++, 33);
    const [w] = env.workers;
    assert.ok(w, 'a worker is made');
    assert.match(env.blob.parts.join(''), /setInterval\(\(\) => postMessage\(0\), e\.data\)/);
    assert.equal(t.running, false);

    t.start();
    t.start(); /* idempotent */
    assert.equal(t.running, true);
    assert.equal(env.intervals.size, 1);
    assert.equal([...env.intervals.values()][0].ms, 33);
    assert.deepEqual(w.posted, [33]);

    /* Either source ticks. */
    [...env.intervals.values()][0].fn();
    w.onmessage({ data: 0 });
    assert.equal(n, 2);

    t.stop();
    t.stop(); /* idempotent */
    assert.equal(t.running, false);
    assert.equal(env.intervals.size, 0);
    assert.deepEqual(w.posted, [33, 0]);
  } finally { teardown(); }
});

test('a worker message that arrives after a stop does nothing', () => {
  const { env, teardown } = install();
  try {
    let n = 0;
    const t = createTicker(() => n++, 33);
    t.start();
    t.stop();
    env.workers[0].onmessage({ data: 0 });
    assert.equal(n, 0);
  } finally { teardown(); }
});

test('destroy stops the clock, ends the worker and releases its source', () => {
  const { env, teardown } = install();
  try {
    const t = createTicker(() => {}, 33);
    t.start();
    t.destroy();
    assert.equal(t.running, false);
    assert.equal(env.workers[0].terminated, true);
    assert.deepEqual(env.revoked, ['blob:ticker']);
  } finally { teardown(); }
});

test('with no Worker the page timer alone is the clock', () => {
  const { env, teardown } = install({ worker: false });
  try {
    let n = 0;
    const t = createTicker(() => n++, 20);
    t.start();
    [...env.intervals.values()][0].fn();
    assert.equal(n, 1);
    t.destroy();
    assert.equal(env.intervals.size, 0);
  } finally { teardown(); }
});

test('a page that refuses the worker falls back to the page timer', () => {
  const { env, teardown } = install({ workerThrows: true });
  try {
    let n = 0;
    const t = createTicker(() => n++, 20);
    assert.deepEqual(env.revoked, ['blob:ticker'], 'the unused source is released');
    t.start();
    [...env.intervals.values()][0].fn();
    assert.equal(n, 1);
    t.destroy();
  } finally { teardown(); }
});
