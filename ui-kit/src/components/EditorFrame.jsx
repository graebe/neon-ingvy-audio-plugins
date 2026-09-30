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
 * list), hint (clauses), bridge (from useEditorBridge), class.
 */
import { createFit, reportHeight } from '../lib/fit.js';
import { createMotion } from '../lib/motion.js';
import { sendMessage } from '../lib/iplug.js';
import { SHELL_MSG } from '../lib/shell.js';
import { Ground } from './Ground.jsx';
import { Hint } from './Hint.jsx';

export function EditorFrame(props) {
  const { scale } = createFit(props.width);
  const [motion, setMotion] = createMotion(props.motionKey);
  reportHeight(() => props.height, scale,
               (h) => sendMessage(SHELL_MSG.height, h));
  return (
    <main class={`window${props.class ? ` ${props.class}` : ''}`}
          style={{ width: `${props.width}px`, height: `${props.height}px`,
                   transform: `scale(${scale()})` }}>
      <Ground enabled={motion()} sources={props.sources}
              ref={(h) => props.bridge?.setGround(h)} />
      {props.children}
      <Hint clauses={props.hint ?? []} motion={motion()} onMotion={setMotion} />
    </main>
  );
}
