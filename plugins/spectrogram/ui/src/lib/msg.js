/*
 * This editor's message tags. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The shell's tags are the kit's (@ultraviolet/ui/shell), the same in every
 * plugin; the rest are this plugin's own vocabulary. tests/editor_tags.test.mjs
 * holds both to the C++.
 */
import { SHELL_MSG } from '@ultraviolet/ui/shell';

/* EMsgTags in Spectrogram.h, after the shell's. */
export const MSG = {
  ...SHELL_MSG,
  cols: 64,   /* <- plugin: "<ch>:<cols>:<bands>:" + raw bytes, oldest column first */
  axis: 65,   /* <- plugin: the band centre frequencies, comma separated */
  /*
   * THE HOST'S CLOCK, every idle tick whether or not a column came with it.
   *
   * "<ppq>:<bpm>:<num>:<denom>:<running>:<ppqPerCol>:<sampleRate>" -- where
   * the transport is, how fast, in what metre, how much musical time one
   * column covers, and the session's rate. The plugin reports these and
   * NOTHING about where a column should be drawn: the bar window is a layout
   * decision and belongs on this side.
   */
  sync: 66,
  /* <- plugin: "<slot>:<live>:<rate>:<label>" a line -- every bus that exists.
   * Sent on a slow timer and on ready: a Listen-In appears when somebody
   * inserts one, which is a human-speed event. */
  sources: 67,
  /* <- plugin: the clash mask, in the same shape as a column batch, tagged with
   * the source it was measured against the own channel. */
  clashCols: 68,
  /* <- plugin, on ready: "<f_min>:<f_max>:<view>:<a>:<b>:<on>:<floor>:<balance>"
   * -- what the session is looking at. Applied before this editor pushes
   * anything (lib/session.js). */
  state: 69,
  range: 96,  /* -> plugin: "<f_min>:<f_max>" -- the zoom */
  /* -> plugin: "<slot>,<slot>,..." -- which buses to open, in order.
   * DERIVED from the view and the comparison rather than being a control of
   * its own: a third list to manage is what made those two feel like one
   * tangled setting. */
  select: 97,
  /* -> plugin: "<ch>,<ch>,..." -- which channels are ADDED into the picture.
   * The plugin sums them and sends one stream, so the editor never routes by
   * channel -- which is what makes "I selected another track and still see the
   * first" impossible rather than merely fixed. */
  view: 99,
  /* -> plugin: "<a>:<b>:<on>" -- the two channels the clash is measured
   * between, and whether it is wanted. Independent of the view. */
  compare: 100,
  /* -> plugin: "<floor_db>:<balance_db>" -- what counts as a clash. */
  clash: 98,
};
