// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The engine's two renders, decoded: the gate across one cycle and the
 * envelope plot's curves (tg_core_render_gate, tg_core_render_envelope).
 *
 * Each is an ASCII header and then one byte a value. In Live they arrived
 * from the plugin; on the Move the page renders them itself (lib/renders.js).
 * Plain JavaScript so node can test them.
 */
import { readHeader, intField, unipolar } from '@ultraviolet/ui/capture';

/**
 * The gate across one cycle as the engine applies it: "<length>:<perStep>:"
 * then a byte of gain per sample -> { length, perStep, values } or null.
 */
export function decodeGate(bytes) {
  const h = readHeader(bytes, 2);
  if (!h) return null;
  const length = intField(h.fields[0]);
  const perStep = intField(h.fields[1]);
  if (!(length >= 1) || !(perStep >= 1)) return null;
  const n = Math.min(length * perStep, bytes.length - h.offset);
  const values = new Float32Array(n);
  for (let i = 0; i < n; i++) values[i] = unipolar(bytes[h.offset + i]);
  return { length, perStep, values };
}

/**
 * The envelope plot's curves: "<steps>:<perStep>:" then the gated curve and
 * the envelope as dialled, a byte of gain a sample each -> { steps, perStep,
 * gated, ghost } or null. x is fractions of a step from the gate opening.
 */
export function decodeEnvelope(bytes) {
  const h = readHeader(bytes, 2);
  if (!h) return null;
  const steps = intField(h.fields[0]);
  const perStep = intField(h.fields[1]);
  if (!(steps >= 1) || !(perStep >= 1)) return null;
  const n = steps * perStep;
  if (bytes.length - h.offset < 2 * n) return null;
  const gated = new Float32Array(n);
  const ghost = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    gated[i] = unipolar(bytes[h.offset + i]);
    ghost[i] = unipolar(bytes[h.offset + n + i]);
  }
  return { steps, perStep, gated, ghost };
}

/**
 * A rendered curve's level `x` steps after the gate opened, linear between
 * samples; past the end, the last sample.
 */
export function levelAt(values, perStep, x) {
  if (!values || !values.length) return 0;
  const f = Math.max(0, x * perStep);
  const i = Math.floor(f);
  if (i >= values.length - 1) return values[values.length - 1];
  return values[i] + (values[i + 1] - values[i]) * (f - i);
}
