/*
 * The shell's message tags: the part of the editor protocol every plugin
 * speaks the same way. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * Mirrors ni::editor::Tag in plugins/_shared/ni/Editor.h, which
 * tests/editor_tags.test.mjs holds this to. Tags 0..nParams-1 are parameter
 * display strings and 64..111 each plugin's own (its lib/msg.js).
 */
export const SHELL_MSG = {
  /* <- plugin: one kick, "<strength>" in 0..1. One message, one ring. */
  ground: 112,
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
