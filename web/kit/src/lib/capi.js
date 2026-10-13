// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * An engine's C ABI, compiled to wasm, called from the page.
 *
 * The engines' browser builds (engines/<p>/crates/<p>-web) export the very
 * `<p>_core_*` functions the plugins link, plus `<p>_web_alloc` / `_free` for
 * the one thing a browser caller cannot bring itself: memory. This is the
 * calling convention around them -- strings in as NUL-terminated bytes,
 * strings and bytes out of a buffer -- so each editor's own code names
 * functions, not pointers.
 *
 *   const c = await loadCapi(url, 'tg');         fetch + instantiate
 *   const c = capiOf(instance, 'tg');            an instance already made
 *   c.fn                                         the raw exports
 *   c.withString(text, (ptr) => ...)             text in, freed after
 *   c.withBuffer(n, (ptr) => len) -> Uint8Array  bytes out, copied
 *   c.text(bytes) -> string
 *
 * MEMORY CAN GROW under any call, and a view over the old buffer is then
 * detached; so every view here is made after the call that filled it.
 */

const encoder = new TextEncoder();
const decoder = new TextDecoder();

export function capiOf(instance, prefix) {
  const fn = instance.exports;
  const alloc = fn[`${prefix}_web_alloc`];
  const free = fn[`${prefix}_web_free`];
  if (typeof alloc !== 'function' || typeof free !== 'function') {
    throw new Error(`not a ${prefix}-web module: no ${prefix}_web_alloc/free`);
  }
  const bytes = () => new Uint8Array(fn.memory.buffer);

  return {
    fn,
    /* `text` as a NUL-terminated string at `ptr` for the length of `use`. */
    withString(text, use) {
      const data = encoder.encode(String(text));
      const n = data.length + 1;
      const ptr = alloc(n);
      try {
        const view = bytes();
        view.set(data, ptr);
        view[ptr + data.length] = 0;
        return use(ptr);
      } finally {
        free(ptr, n);
      }
    },
    /* A buffer of `n` bytes for `fill` to write; what it says it wrote (a
     * negative count is nothing), copied out before the buffer is freed. */
    withBuffer(n, fill) {
      const ptr = alloc(n);
      try {
        const len = fill(ptr, n);
        if (!(len > 0)) return null;
        return bytes().slice(ptr, ptr + Math.min(len, n));
      } finally {
        free(ptr, n);
      }
    },
    text: (data) => (data ? decoder.decode(data) : ''),
  };
}

export async function loadCapi(url, prefix) {
  const res = await fetch(url);
  const { instance } = await WebAssembly.instantiate(await res.arrayBuffer(), {});
  return capiOf(instance, prefix);
}
