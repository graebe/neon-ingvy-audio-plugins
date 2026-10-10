// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What to download, without a version typed on a web page.
 *
 * THREE SOURCES, AND NONE OF THEM IS THIS FILE.
 *
 * The plugin's download is the newest PUBLISHED, STABLE release of the
 * product that is not ahead of versions.json (scripts/release.mjs,
 * latestStableRelease). It used to be versions.json's version as it stood,
 * which runs ahead of what is released whenever a bump is not tagged yet and
 * may be a beta -- a link to a release that did not exist, or a beta offered
 * as the download. The releases are GitHub's on the Pages runner
 * (NI_RELEASES, .github/workflows/pages.yml) and the checkout's tags in a
 * local build.
 *
 * release.json is the Move module's contract with the Schwung catalog: the
 * catalog carries no version and the manager resolves the download from that
 * file at install time. Reading the same file means the version shown here and
 * the version a device installs cannot disagree. It is read at build time, so
 * there is no API, no rate limit and no token.
 */
import versions from '../../../versions.json' with { type: 'json' };
import release from '../../../release.json' with { type: 'json' };
import { latestStableRelease, publishedReleases } from '../../../scripts/release.mjs';

const REPO = 'https://github.com/graebe/neon-ingvy-audio-plugins';

/*
 * The bundle zips for a product, one per OS, as release-plugins.yml names
 * them: the tag is <product>-<version as versions.json spells it> --
 * scripts/release.mjs is the parser both workflows share, and its ZIP_OS the
 * OS labels. This used to put a second "v" in front of a version that already
 * begins with one, a link to trance-gate-vv2026.09.29.3.
 *
 * macOS only: Linux and Windows follow in a later release, and get their
 * buttons back then.
 */
const ZIPS = [
  { os: 'macOS', label: 'macOS · universal' },
];

const RELEASES = publishedReleases();

export function pluginDownloads(product) {
  /* Only a release that carries every zip linked below. */
  const version = latestStableRelease(product, RELEASES, versions[product], ZIPS.map(({ os }) => `-${os}.zip`));
  if (!version) return [];
  const tag = `${product}-${version}`;
  return ZIPS.map(({ os, label }) => ({
    version,
    label,
    url: `${REPO}/releases/download/${tag}/${product}-${version}-${os}.zip`,
  }));
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
 * A product's Move module: release.json's STABLE entry, never its beta
 * channel. release.json records only what has been published (the release
 * workflow writes it), so there is nothing untagged to leave out.
 */
export function moduleDownload(product) {
  const entry = release.modules?.[moduleIds[product]];
  if (!entry) return null;
  const stable = entry.channels?.stable ?? entry;
  if (!stable?.version) return null;
  return { version: stable.version, url: stable.download_url };
}

export { versions };
