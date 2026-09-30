/*
 * Copy the assets the site serves but does not own.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * THE FONT'S LICENCE. ui-kit/src/tokens.css names the kit's own WOFF files by a
 * relative URL, so the site's build resolves and emits them itself; what it
 * cannot know is that the SIL Open Font License has to travel with them.
 * OFL.txt is copied to /fonts/OFL.txt, where the licences page links it and
 * where THIRD_PARTY_LICENSES.md promises it is. This site is a bundle.
 */
import { cpSync, mkdirSync, existsSync, rmSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const SITE = dirname(dirname(fileURLToPath(import.meta.url)));
const ROOT = dirname(SITE);
const PUBLIC = join(SITE, 'public');

const need = (p, what) => {
  if (!existsSync(p)) {
    console.error(`stage-assets: ${what} is missing at ${p}`);
    process.exit(1);
  }
  return p;
};

// The licence, from the kit that owns the font.
const ofl = need(join(ROOT, 'ui-kit/src/fonts/OFL.txt'), 'the SIL Open Font License notice');
rmSync(join(PUBLIC, 'fonts'), { recursive: true, force: true });
mkdirSync(join(PUBLIC, 'fonts'), { recursive: true });
cpSync(ofl, join(PUBLIC, 'fonts/OFL.txt'));

// Screenshots and any other generated media.
const media = join(ROOT, 'docs/media');
if (existsSync(media)) {
  rmSync(join(PUBLIC, 'media'), { recursive: true, force: true });
  cpSync(media, join(PUBLIC, 'media'), { recursive: true });
}

console.log('stage-assets: the font licence and media staged into site/public/');
