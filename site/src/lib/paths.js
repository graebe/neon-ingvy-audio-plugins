// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The one place an internal URL is spelled.
 *
 * import.meta.env.BASE_URL is "/neon-ingvy-audio-plugins/" in both dev and production,
 * because Astro applies `base` to the dev server too -- which is the only
 * reason a mistake here is catchable before a deploy.
 */
export const url = (p = '') =>
  `${import.meta.env.BASE_URL}/${p}`.replace(/\/{2,}/g, '/');
