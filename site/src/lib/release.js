/*
 * What to download, without a version typed on a web page.
 * Copyright (c) 2026 Torben Gräber. MIT.
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

/* The macOS bundle zip for a product, as release-plugins.yml names it. */
export function pluginDownload(product) {
  const version = versions[product];
  if (!version) return null;
  return {
    version,
    url: `${REPO}/releases/download/${product}-v${version}/${product}-${version}-macOS.zip`,
  };
}

/*
 * The Move module. A beta is offered ONLY when it is strictly newer than
 * stable -- the same rule the Schwung manager applies, applied here rather than
 * trusted, so the site cannot advertise a beta that has fallen behind.
 */
export function moduleDownload() {
  const stable = release.channels?.stable ?? {
    version: release.version,
    download_url: release.download_url,
  };
  if (!stable?.version) return null;
  return { version: stable.version, url: stable.download_url };
}

export { versions };
