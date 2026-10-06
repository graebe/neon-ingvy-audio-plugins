/*
 * The slot files: export this slot or all eight, import either.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * WINDOW VERBS, so they sit together as one joined group and never among the
 * knobs -- under the envelope plot, flush with the panels' bottom edge, the one
 * place in this window with room for three words. A word each: the design's
 * glyph set has none for a file. What IMPORT replaces is decided by the file
 * (a slot or a bank), so it needs no choice of its own. The plugin shows the
 * system's panel and answers with how it went, which the hint bar shows.
 */
import { Button, sendMessage } from '@ultraviolet/ui';
import { MSG } from './msg.js';
import { INFO } from './info.js';

export function SlotFiles() {
  return (
    <div class="slot-files btn-group" role="group" aria-label="Slot files">
      <Button info={INFO.exportSlot}
              onClick={() => sendMessage(MSG.exportFile, 'slot')}>EXPORT</Button>
      <Button info={INFO.exportAll}
              onClick={() => sendMessage(MSG.exportFile, 'bank')}>EXPORT ALL</Button>
      <Button info={INFO.import}
              onClick={() => sendMessage(MSG.importFile)}>IMPORT</Button>
    </div>
  );
}
