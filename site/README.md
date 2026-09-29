# site

The public documentation site: <https://graebe.github.io/vst-library/>

Astro, static, no framework islands. It draws the Ultraviolet design system by
importing `@ultraviolet/ui/tokens.css` through the npm workspace — the same file
both editors import, not a copy of it.

## It renders the repository's Markdown

There is no second copy of any product's documentation. The content collections
in `src/content.config.ts` reach **out** of this directory:

| collection | source |
|---|---|
| `plugins` | `plugins/*/README.md` |
| `pluginDocs` | `plugins/*/docs/*.md` |
| `tech` | `docs/tech/*.md` |

So the text someone reads on GitHub and the text on the site are the same text,
and they cannot drift. `plugins/remark-repo-urls.mjs` is what makes that work in
both directions: the links in those files are relative paths between repository
files, and it rewrites each one to the route, the staged image or the GitHub
blob it means here.

## Running it

```sh
npm run dev --workspace site        # http://localhost:4321/vst-library/
npm run build --workspace site
npm run check --workspace site      # every internal link resolves
npm run preview --workspace site    # the built site, base path and all
```

`prebuild` and `predev` run `scripts/stage-assets.mjs`, which copies the fonts
and `OFL.txt` into `public/` — `tokens.css` declares `@font-face` with a
`../fonts/` URL that no consumer resolves at build time, so each one satisfies it
from its own served root. `docs/media/` is staged the same way.

## The base path is the trap

The site is served under `/vst-library/`. A hand-written `href="/tech/"` resolves
to `graebe.github.io/tech/` and 404s — and only on the deployed site, because
`astro dev` and `astro preview` both apply the base. So:

- every internal URL goes through `src/lib/paths.js`
- every Markdown link goes through the remark plugin
- `scripts/check-links.mjs` walks the built HTML and fails if one slipped
  through, in CI before the upload and as `ctest -R site_links`

## Screenshots

```sh
sh site/scripts/screenshots.sh      # -> docs/media/<product>/live.png
```

Headless Chrome against each editor's own review harness, which lays the
**shipping** bundle beside its page. So the picture on the site is the editor the
plugin carries, and regenerating after a UI change keeps it that way.

## Deploying

`.github/workflows/pages.yml`, on a push to `main` that touches documentation and
on every published release. **One manual step, once:** repository Settings →
Pages → Source must be **GitHub Actions**.
