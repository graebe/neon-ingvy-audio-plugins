/*
 * This editor's message tags. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * NOT IN THE KIT, and that is the seam: @ultraviolet/ui carries the iPlug2
 * bridge because it is identical for every plugin, but EMsgTags is this
 * plugin's own vocabulary and has nothing to say to any other.
 */
/*
 * THE MESSAGE TAGS, mirroring EMsgTags in Spectrogram.h.
 */
export const MSG = {
  cols: 64,   /* <- plugin: "<cols>:<bands>:<hex>", oldest column first */
  axis: 65,   /* <- plugin: the band centre frequencies, comma separated */
  /*
   * THE HOST'S CLOCK, every idle tick whether or not a column came with it.
   *
   * "<ppq>:<bpm>:<num>:<denom>:<running>:<ppqPerCol>" -- where the transport is,
   * how fast, in what metre, and how much musical time one column covers. The
   * plugin reports these and NOTHING about where a column should be drawn: the
   * bar window is a layout decision and belongs on this side.
   */
  sync: 66,
  range: 96,  /* -> plugin: "<f_min>:<f_max>" -- the zoom */
  /*
   * "I AM LISTENING", and it has to exist because the plugin's push on open
   * CANNOT be heard.
   *
   * OnUIOpen fires from didFinishNavigation and sends the frequency axis. But
   * this editor is a <script type="module">, module scripts are DEFERRED, and
   * so they evaluate AFTER the document is done -- globalThis.SAMFD does not
   * exist yet and the axis is dropped. The picture would then roll correctly
   * with no numbers beside it.
   *
   * Sent from onMount, so it cannot be early.
   */
  ready: 102,
};
