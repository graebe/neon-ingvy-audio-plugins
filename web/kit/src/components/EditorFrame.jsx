// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The window every editor is drawn in.
 *
 * What the design system gives EVERY window, so no editor lays it out itself:
 * the content from the top ("nothing is centred vertically"), the Hint bar
 * pinned to the bottom edge with the Signature, the window padding, and the
 * scale that fits a fixed design width into the viewport -- with the height
 * the editor needs reported to the plugin whenever the design height or the
 * scale moves. The window ground is the page's own (`body`, tokens.css).
 *
 * Props: width, height (design px), hint (clauses), status (an action's
 * outcome as one clause, or null), signatureInfo (the bar's own string),
 * class.
 *
 * THE INFO STRINGS ARE THE WINDOW'S, so they are wired here once: every
 * element inside it with a `data-info` puts its string in the hint bar while it
 * is pointed at or focused (lib/info.js has the rules and the precedence).
 */
import { createContext, useContext, onMount, onCleanup } from 'solid-js';
import { createFit, reportHeight } from '../lib/fit.js';
import { createInfo, bindInfo, hintClauses } from '../lib/info.js';
import { sendMessage } from '../lib/host.js';
import { SHELL_MSG } from '../lib/shell.js';
import { Hint } from './Hint.jsx';

/* What a component inside the window may need of it: the scale, for a canvas
 * that sizes its backing store in device pixels. */
const FrameContext = createContext(null);
export const useFrame = () => useContext(FrameContext);

export function EditorFrame(props) {
  const { scale } = createFit(props.width);
  reportHeight(() => props.height, scale,
               (h) => sendMessage(SHELL_MSG.height, h));
  const info = createInfo();
  let root;
  onMount(() => onCleanup(bindInfo(root, info)));
  const bar = () => hintClauses({ conventions: props.hint ?? [], info: info.text(),
                                  status: props.status });
  return (
    <main ref={root} class={`window${props.class ? ` ${props.class}` : ''}`}
          style={{ width: `${props.width}px`, height: `${props.height}px`,
                   transform: `scale(${scale()})` }}>
      <FrameContext.Provider value={{ scale }}>{props.children}</FrameContext.Provider>
      <Hint clauses={bar().clauses} info={bar().info} signatureInfo={props.signatureInfo} />
    </main>
  );
}
