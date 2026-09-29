/*
 * The one place an internal URL is spelled.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * import.meta.env.BASE_URL is "/vst-library/" in both dev and production,
 * because Astro applies `base` to the dev server too -- which is the only
 * reason a mistake here is catchable before a deploy.
 */
export const url = (p = '') =>
  `${import.meta.env.BASE_URL}/${p}`.replace(/\/{2,}/g, '/');
