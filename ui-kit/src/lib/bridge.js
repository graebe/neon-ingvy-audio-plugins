/*
 * The handshake every editor makes with its plugin.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * The plugin pushes its state from OnUIOpen, which fires before a deferred
 * module script has run -- so every one of those pushes lands on globals that
 * do not exist yet and is dropped. The editor therefore asks (SHELL_MSG.ready)
 * once it is listening, and the plugin answers with everything: the defaults,
 * every value, every display string, then its own messages.
 *
 * Four editors each wrote that, plus the same few lines handing a ring to the
 * ground. This is the one copy. Call it in the editor's body, after any
 * createParams: its listeners are live at once, and `ready` goes out from
 * onMount -- after every child has mounted and registered its own.
 */
import { onMount, onCleanup } from 'solid-js';
import { onMessage, onBytes, sendMessage } from './iplug.js';
import { SHELL_MSG } from './shell.js';

/** A ring's strength, or null for a payload that is not one. */
export const parseGround = (text) => {
  const s = Number.parseFloat(text);
  return Number.isFinite(s) ? s : null;
};

/**
 * @param opts.onMessage (tag, text) for every text message but the ground's
 * @param opts.bytes     { [tag]: (Uint8Array) => void } for the binary tags
 * @returns { setGround } -- hand it to <Ground ref> (EditorFrame does)
 */
export function useEditorBridge(opts = {}) {
  let ground = null;
  const offs = [
    onMessage((tag, text) => {
      if (tag === SHELL_MSG.ground) {
        /* One message, one ring; a malformed payload is not a full-strength ring. */
        const s = parseGround(text);
        if (s !== null) ground?.trigger(s);
        return;
      }
      if (tag === SHELL_MSG.groundTick) {
        ground?.tick?.();
        return;
      }
      opts.onMessage?.(tag, text);
    }),
    ...Object.entries(opts.bytes ?? {}).map(([tag, fn]) => onBytes(Number(tag), fn)),
  ];
  onMount(() => sendMessage(SHELL_MSG.ready));
  onCleanup(() => offs.forEach((off) => off()));
  return {
    setGround: (handle) => {
      ground = handle;
      /* The field says when it moves; the plugin sends frame ticks while it
       * does. */
      handle?.onRunning?.((on) => sendMessage(SHELL_MSG.groundRun, on ? '1' : '0'));
    },
  };
}
