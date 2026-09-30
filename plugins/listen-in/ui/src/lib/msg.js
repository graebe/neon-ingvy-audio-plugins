/*
 * This editor's message tags. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The shell's tags are the kit's (@ultraviolet/ui/shell), the same in every
 * plugin; the rest are this plugin's own vocabulary. tests/editor_tags.test.mjs
 * holds both to the C++.
 */
import { SHELL_MSG } from '@ultraviolet/ui/shell';

/* EMsgTags in ListenIn.h, after the shell's. */
export const MSG = {
  ...SHELL_MSG,
  state: 64,  /* <- plugin: "<slot>:<status>:<peak>"        */
  label: 96,  /* <-> plugin: the display name, both ways    */
};

/* Mirroring listenin::wire::Status in Wire.h. */
export const STATUS = {
  idle: 0,
  live: 1,
  taken: 2,
  unavailable: 3,
};
