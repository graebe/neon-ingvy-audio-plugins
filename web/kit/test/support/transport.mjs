// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/* A transport that records what the editor sends and can speak for the host
 * (lib/host.js): `t.in.param(i, v)`, `t.in.message(tag, text)`. */
import { connect } from '../../src/lib/host.js';

export function recorder() {
  const t = { sent: [], in: null };
  t.attach = (inbound) => { t.in = inbound; };
  t.setParam = (i, v) => t.sent.push(['set', i, v]);
  t.beginGesture = (i) => t.sent.push(['begin', i]);
  t.endGesture = (i) => t.sent.push(['end', i]);
  t.sendMessage = (tag, text) => t.sent.push(['msg', tag, text]);
  connect(t);
  return t;
}
