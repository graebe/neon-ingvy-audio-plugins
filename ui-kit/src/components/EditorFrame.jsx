/*
 * The window every editor is drawn in.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * What the design system gives EVERY window, so no editor lays it out itself:
 * the ground as its first child, the content from the top ("nothing is centred
 * vertically"), the Hint bar pinned to the bottom edge with the Signature and
 * the Motion switch, the window padding, and the scale that fits a fixed design
 * width into the viewport -- with the height the editor needs reported to the
 * plugin whenever the design height or the scale moves.
 *
 * Props: width, height (design px), motionKey, sources (the Ground's selector
 * list), hint (clauses), status (an action's outcome as one clause, or null),
 * motionInfo and signatureInfo (the bar's own two strings), bridge (from
 * useEditorBridge), class.
 *
 * THE INFO STRINGS ARE THE WINDOW'S, so they are wired here once: every
 * element inside it with a `data-info` puts its string in the hint bar while it
 * is pointed at or focused (lib/info.js has the rules and the precedence).
 */
import { createContext, useContext, onMount, onCleanup } from 'solid-js';
import { createFit, reportHeight } from '../lib/fit.js';
import { createMotion } from '../lib/motion.js';
import { createInfo, bindInfo, hintClauses } from '../lib/info.js';
import { sendMessage } from '../lib/iplug.js';
import { SHELL_MSG } from '../lib/shell.js';
import { Ground } from './Ground.jsx';
import { Hint } from './Hint.jsx';

/* What a component inside the window may need of it: the scale, for a canvas
 * that sizes its backing store in device pixels. */
const FrameContext = createContext(null);
export const useFrame = () => useContext(FrameContext);

export function EditorFrame(props) {
  const { scale } = createFit(props.width);
  const [motion, setMotion] = createMotion(props.motionKey);
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
      <Ground enabled={motion()} sources={props.sources}
              ref={(h) => props.bridge?.setGround(h)} />
      <FrameContext.Provider value={{ scale }}>{props.children}</FrameContext.Provider>
      <Hint clauses={bar().clauses} info={bar().info} motion={motion()} onMotion={setMotion}
            motionInfo={props.motionInfo} signatureInfo={props.signatureInfo} />
    </main>
  );
}
