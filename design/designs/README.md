# design/designs

A mirror of the **NI Plugin Layouts** canvas, artifact
`3yBz5KS6LVCBBHSmbae13v` (the `Designs:` line in `design/public-link`). The
canvas draws every plugin window twice: as built, and as proposed on
Ultraviolet.

Every file here is the artifact's `project/` tree, byte for byte:

- `canvas.json`: the boards, their positions, the sticky notes and the design
  system the canvas was made with.
- `*.dc.html`: one page per artboard.
- `uv-tokens.css` and `ds/ultraviolet/`: the Ultraviolet tokens and component
  bundle the artboards load. The canvas carries its own copy of the design
  system, so these duplicate files in `design/scheme/project`; they stay,
  because the mirror has to match the artifact, not the repo.

Nothing in the build reads this folder. It is a reference for the editors in
`plugins/*/ui`; the values they use come from `design/scheme/project`.

## Re-syncing

1. Read the artifact's file listing (`Artifact` `list`, scope `files`, on the
   link in `design/public-link`).
2. Fetch every listed path and replace the files here with them; delete any
   file the listing no longer has.
3. Add nothing else apart from this README: the canvas's link lives in
   `design/public-link`, not in a file of its own.
