/*
 * Which x ranges `count` columns written into a ring of `cols` from `start`
 * touched: one span, or two when the write wraps. Copyright (c) 2026 Torben Gräber. MIT.
 */
export function ringSpans(start, count, cols) {
  const n = Math.min(Math.max(0, count), cols);
  if (n === 0) return [];
  const end = start + n - 1;
  return end < cols ? [[start, end]] : [[start, cols - 1], [0, end - cols]];
}
