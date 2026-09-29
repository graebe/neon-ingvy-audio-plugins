/*
 * This editor's message tags. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * NOT IN THE KIT, and that is the seam: @ultraviolet/ui carries the iPlug2
 * bridge because it is identical for every plugin, but EMsgTags is this
 * plugin's own vocabulary and has nothing to say to any other.
 */
/*
 * THE MESSAGE TAGS, mirroring EMsgTags in TranceGate.h. Tags 0..11 are a
 * parameter's display string, tagged with the parameter's own index.
 */
export const MSG = {
  uiState: 64, params: 65, scope: 66, patch: 67,
  setStep: 96, setDepth: 97, setCursor: 98, requestPatch: 99,
  setText: 100, rows: 101,
  /*
   * "I AM LISTENING", and it has to exist because the plugin's push on open
   * CANNOT be heard.
   *
   * OnUIOpen fires from didFinishNavigation and calls SPVFD() twelve times.
   * But this editor is a <script type="module">, module scripts are DEFERRED,
   * and so they evaluate AFTER the document is done -- globalThis.SPVFD does
   * not exist yet and all twelve values are dropped. The UI then sat on
   * twelve zeroes until something was touched, which showed up as four
   * separate faults: a knob whose first drag jumped to zero, a switch drawn
   * off whatever the engine held, and two dropdowns stuck on their first
   * entry.
   *
   * Sent from onMount, so it cannot be early.
   */
  ready: 102,
};
