/*
 * Copy the assets the site serves but does not own.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * THE FONTS ARE A CONTRACT, not a convenience. ui-kit/src/tokens.css declares
 * @font-face with url('../fonts/...'), which no consumer resolves at build
 * time -- vite says so out loud and leaves the URL alone. Each consumer is
 * expected to satisfy it from its own served root: the plugin editors do it
 * with their public/fonts, and this does it for the site. Emitted CSS lands in
 * _astro/, so '../fonts/' resolves to the site root, which is where these land.
 *
 * OFL.txt travels with them because the licence requires it to, and because
 * THIRD_PARTY_LICENSES.md promises it does in every bundle. This site is a
 * bundle.
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

// The fonts, from whichever editor holds them -- they are byte-identical
// copies of one family and the token file is the thing that names them.
const fonts = need(
  join(ROOT, 'plugins/trance-gate/ui/public/fonts'),
  'the JetBrains Mono family',
);
rmSync(join(PUBLIC, 'fonts'), { recursive: true, force: true });
mkdirSync(join(PUBLIC, 'fonts'), { recursive: true });
cpSync(fonts, join(PUBLIC, 'fonts'), { recursive: true });
need(join(PUBLIC, 'fonts/OFL.txt'), 'the SIL Open Font License notice');

// Screenshots and any other generated media.
const media = join(ROOT, 'docs/media');
if (existsSync(media)) {
  rmSync(join(PUBLIC, 'media'), { recursive: true, force: true });
  cpSync(media, join(PUBLIC, 'media'), { recursive: true });
}

console.log('stage-assets: fonts and media staged into site/public/');
