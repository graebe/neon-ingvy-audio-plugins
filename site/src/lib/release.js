// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What to download, without a version typed on a web page.
 *
 * TWO SOURCES, AND NEITHER IS THIS FILE.
 *
 * versions.json decides every product's version and `ctest -R versions` holds
 * config.h, module.json and every crate to it -- so composing the plugin's
 * download URL from it means this page cannot offer a version the tree does not
 * build.
 *
 * release.json is the Move module's contract with the Schwung catalog: the
 * catalog carries no version and the manager resolves the download from that
 * file at install time. Reading the same file means the version shown here and
 * the version a device installs cannot disagree. It is read at build time, so
 * there is no API, no rate limit and no token.
 */
import versions from '../../../versions.json' with { type: 'json' };
import release from '../../../release.json' with { type: 'json' };

const REPO = 'https://github.com/graebe/neon-ingvy-audio-plugins';

/*
 * The macOS bundle zip for a product, as release-plugins.yml names it: the tag
 * is <product>-<version as versions.json spells it> -- scripts/release.mjs is
 * the parser both workflows share. This used to put a second "v" in front of a
 * version that already begins with one, a link to trance-gate-vv2026.09.29.3.
 */
export function pluginDownload(product) {
  const version = versions[product];
  if (!version) return null;
  const tag = `${product}-${version}`;
  return {
    version,
    url: `${REPO}/releases/download/${tag}/${product}-${version}-macOS.zip`,
  };
}

/*
 * The catalog id of each product's Move module. release.json is keyed by it
 * (Schwung's multi-module shape) and the id is not the directory name: the
 * Side-Chain's is ni-side-chain.
 */
const moduleIds = Object.fromEntries(
  Object.entries(import.meta.glob('../../../modules/*/module.json', { eager: true, import: 'default' }))
    .map(([path, json]) => [path.split('/').at(-2), json.id]));

/*
 * A product's Move module. A beta is offered ONLY when it is strictly newer
 * than stable -- the same rule the Schwung manager applies, applied here rather
 * than trusted, so the site cannot advertise a beta that has fallen behind.
 * Only stable is shown, so that rule has nothing to do yet.
 */
export function moduleDownload(product) {
  const entry = release.modules?.[moduleIds[product]];
  if (!entry) return null;
  const stable = entry.channels?.stable ?? entry;
  if (!stable?.version) return null;
  return { version: stable.version, url: stable.download_url };
}

export { versions };
