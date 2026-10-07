// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The fifteen glyphs, taken from the design system's own files at build time.
 *
 * NOT COPIED BY HAND. Vite reads design/scheme/project/assets/Icons/*.svg --
 * the vendored, byte-faithful mirror of the published system -- and inlines
 * them, so a re-vendored icon arrives in every editor on its next build.
 */
import { iconBody, iconName } from './icon-svg.js';

const files = import.meta.glob('../../../design/scheme/project/assets/Icons/*.svg',
  { query: '?raw', import: 'default', eager: true });

export const ICONS = Object.fromEntries(
  Object.entries(files).map(([path, svg]) => [iconName(path), iconBody(svg)])
    .filter(([, body]) => body));
