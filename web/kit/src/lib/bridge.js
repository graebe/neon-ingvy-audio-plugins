// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The handshake every editor makes with its plugin.
 *
 * The plugin pushes its state from OnUIOpen, which fires before a deferred
 * module script has run -- so every one of those pushes lands on globals that
 * do not exist yet and is dropped. The editor therefore asks (SHELL_MSG.ready)
 * once it is listening, and the plugin answers with everything: the defaults,
 * every value, every display string, then its own messages.
 *
 * Four editors each wrote that. This is the one copy. Call it in the
 * editor's body, after any
 * createParams: its listeners are live at once, and `ready` goes out from
 * onMount -- after every child has mounted and registered its own.
 */
import { onMount, onCleanup } from 'solid-js';
import { onMessage, onBytes, sendMessage } from './host.js';
import { SHELL_MSG } from './shell.js';

/**
 * @param opts.onMessage (tag, text) for every text message
 * @param opts.bytes     { [tag]: (Uint8Array) => void } for the binary tags
 */
export function useEditorBridge(opts = {}) {
  const offs = [
    onMessage((tag, text) => opts.onMessage?.(tag, text)),
    ...Object.entries(opts.bytes ?? {}).map(([tag, fn]) => onBytes(Number(tag), fn)),
  ];
  onMount(() => sendMessage(SHELL_MSG.ready));
  onCleanup(() => offs.forEach((off) => off()));
}
