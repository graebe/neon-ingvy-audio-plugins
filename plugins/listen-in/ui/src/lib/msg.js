/*
 * This editor's message tags. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * NOT IN THE KIT, and that is the seam: @ultraviolet/ui carries the iPlug2
 * bridge because it is identical for every plugin, but EMsgTags is this
 * plugin's own vocabulary and has nothing to say to any other.
 *
 * Mirroring EMsgTags in ListenIn.h.
 */
export const MSG = {
  state: 64,  /* <- plugin: "<slot>:<status>:<peak>"        */
  label: 96,  /* <-> plugin: the display name, both ways    */
  /*
   * "I AM LISTENING", and it has to exist because the plugin's push on open
   * CANNOT be heard.
   *
   * OnUIOpen fires from didFinishNavigation and sends the state. But this
   * editor is a <script type="module">, module scripts are DEFERRED, and so
   * they evaluate AFTER the document is done -- globalThis.SAMFD does not exist
   * yet and the state is dropped. The window would then show slot 1, idle,
   * whatever the plugin actually holds.
   *
   * Sent from onMount, so it cannot be early.
   */
  ready: 102,
};

/* Mirroring listenin::wire::Status in Wire.h. */
export const STATUS = {
  idle: 0,
  live: 1,
  taken: 2,
  unavailable: 3,
};
