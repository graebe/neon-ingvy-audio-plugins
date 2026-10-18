// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The shell's message tags: the part of the editor protocol every web editor
 * speaks the same way, through its host (host.js; on the Move, schwung.js).
 *
 * The tags were the iPlug2 bridge's, whose C++ side went with ADR 0002; the
 * web kit keeps them as its own vocabulary (ADR 0004). Tags 0..nParams-1 are
 * parameter display strings and 64..111 each editor's own (its lib/msg.js).
 * 112, 114 and 123 were the animated ground's, and stay unused.
 */
export const SHELL_MSG = {
  /* <- plugin: every parameter's default, normalised, "<d0>:<d1>:...", in
   * parameter-index order -- what a reset sets. Sent with the state. */
  defaults: 113,
  /* -> plugin: mounted and listening -- send the whole state. A push from the
   * plugin's OnUIOpen lands before a module script has run. */
  ready: 120,
  /* -> plugin: "<paramIdx>:<typed text>", parsed by the parameter itself. */
  setText: 121,
  /* -> plugin: the height the editor needs, in viewport pixels. */
  height: 122,
};
