// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The content lives in the repository, not in this directory.
 *
 * A product's manual is the README.md beside its source, and the two interface
 * documents are in its docs/ folder. This site renders those files; it does not
 * hold copies of them. So the same text is read by someone browsing the
 * repository on GitHub and by someone reading the site, and the two cannot
 * drift -- which is the entire reason for the `base: '../plugins'` below.
 *
 * glob()'s base resolves relative to the Astro project root and may climb out
 * of it; the loader reads with node:fs at build time, so no symlink and no copy
 * step is involved. A copy would reintroduce exactly the drift this avoids.
 */
import { defineCollection, z } from 'astro:content';
import { glob } from 'astro/loaders';

const plugins = defineCollection({
  loader: glob({
    base: '../plugins',
    pattern: '*/README.md',
    generateId: ({ entry }) => entry.split('/')[0],
  }),
  schema: z.object({
    title: z.string(),
    tagline: z.string(),
    order: z.number().default(99),

    // Which shells exist. The Spectrogram has one.
    hosts: z.array(z.enum(['live', 'move'])).min(1),
    formats: z.array(z.enum(['VST3'])).default([]),

    // Provenance, rendered as a fact table.
    engine: z.string(),
    crates: z.array(z.string()),
    tests: z.array(z.string()).default([]),

    // When hosts excludes 'move', this says why. An absence explains nothing;
    // a sentence does.
    notOnMove: z.string().optional(),

    // Media, as public-relative strings. NOT image(): that resolves relative to
    // the entry file, and every entry here lives outside the Astro root.
    still: z.string().optional(),
    audio: z.string().optional(),
  }),
});

const pluginDocs = defineCollection({
  loader: glob({
    base: '../plugins',
    pattern: '*/docs/*.md',
    generateId: ({ entry }) => entry.replace('/docs/', '/').replace(/\.md$/, ''),
  }),
  /*
   * `section` is a typed field rather than a heading in the prose, because
   * splitting rendered Markdown on heading text fails SILENTLY: reword the
   * heading and the section renders empty. A missing enum value fails loudly,
   * at build time, with the file named.
   */
  schema: z.object({
    section: z.enum(['live', 'schwung']),
    title: z.string(),
  }),
});

const tech = defineCollection({
  loader: glob({ base: '../docs/tech', pattern: '*.md' }),
  schema: z.object({
    title: z.string(),
    order: z.number(),
    slug: z.string(),
  }),
});

/*
 * music-core's tutorial, from the crate's own docs/ folder for the reason the
 * plugins' manuals are read from theirs: one text, read on GitHub and here.
 * Same shape as tech: one page, a section per file.
 */
const musicCore = defineCollection({
  loader: glob({ base: '../engines/shared/crates/music-core/docs', pattern: '*.md' }),
  schema: z.object({
    title: z.string(),
    order: z.number(),
    slug: z.string(),
  }),
});

/*
 * The changelog is the repository's CHANGELOG.md, one file and no frontmatter:
 * it is read on GitHub as often as here, and the version headings ARE its
 * structure.
 */
const changelog = defineCollection({
  loader: glob({ base: '..', pattern: 'CHANGELOG.md', generateId: () => 'changelog' }),
  schema: z.object({}),
});

export const collections = { plugins, pluginDocs, tech, musicCore, changelog };
