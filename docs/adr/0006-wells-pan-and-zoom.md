# 0006. A well may pan and zoom its content; the window still never scrolls

- Status: accepted, 2026-10-11 (with the owner's approval of the first
  editor board whose well pans); amended 2026-10-16 (a second zoom axis, a
  ruler's fit)
- Decided by: Torben Gräber
- Amends: the design system's "Windows never scroll" (Spacing and layout) and
  the rules every editor keeps ([native UI](../tech/native-ui.md))

## Context

Every editor so far fits its content into one fixed design size:

- `FixedDesign` scales the window from the top-left;
- nothing reflows, nothing scrolls;
- the nearest thing to a moving view is the Chord-Detector's history, whose
  "now" is pinned to the right edge.

A note editor of a recording shows minutes of notes, each a few hundred
milliseconds long, over two octaves or more. No fixed scale shows a
three-minute song and a 20-cent pitch drag at once. Such an editor must be
able to look at part of its content in detail.

## Decision

**The window stays as it is:** one fixed design size, scaled whole, never
scrolled, never reflowed.

**A well may pan and zoom what it shows.** Its frame, its place and its size
stay fixed; only the view of its content moves.

- **Gestures.**
  - Wheel or trackpad: pan.
  - Shift-wheel: horizontal pan.
  - Pinch or Cmd-wheel: zoom about the pointer.
  - Alt-pinch or Alt-Cmd-wheel: zoom the second axis about the pointer, in
    a well that zooms two.
  - Drag on a ruler: pan.
  - Double-click on a ruler: fit its axis to the content in view, with room
    either side.
  - Drag on an overview: move the view.
- **Keys**, while the well has keyboard focus:
  - arrows step, as everywhere;
  - `+` and `-` zoom; with Alt, the second axis. The key is read, not the
    character it types: Alt-= types a not-equal sign;
  - Home and End go to the content's ends;
  - Space stays the host's.
- **Orientation is never lost.**
  - A ruler on each axis that moves shows where the view is: bars and beats,
    pitch.
  - An overview shows the whole content with the view's rectangle on it.
- **Limits.** Zoom stops at the content's ends and at a minimum and maximum
  scale, which each well sets for its content. A well never shows more than
  a margin beyond its content.

The zoom and pan maths is shared in the kit (`ZoomMath.h`: a pan, a zoom
about an anchor, a fit, the grid), tested without a window, and used by
every well that pans and its rulers (`TimeView`, `PitchView`, `NoteBars`,
`Overview`).

## Consequences

- **The rule is narrower than "scrolling".** The window never moves; only a
  well's view does. [native UI](../tech/native-ui.md) states it so.
- **Wells that pan need state the window did not have**: the view's span. It
  belongs to the editor, not to the product's state, and is not undoable.
- **Snapshot tests** of a panning well fix its view in the fake before they
  compare.
