# 0007. An editor with its own document undoes with Cmd-Z

- Status: accepted, 2026-10-11 (with the owner's approval of the first
  editor board that edits a document of its own)
- Decided by: Torben Gräber
- Amends: the keyboard conventions (`Keys.h`, the design README's
  Interaction conventions), where no shortcut uses Cmd or Ctrl

## Context

So far every edit an editor makes is either a parameter change, which the
host undoes through its own gestures, or a small state change that only
marks the set unsaved (a pasted Trance Gate pattern). No editor has an undo of
its own, and no shortcut uses Cmd or Ctrl: the Trance Gate's `Pads.cpp` and
the kit's `Select.cpp` explicitly let modified keys go to the host.

An editor can edit a document the host cannot see: the notes of a recording,
say. Its edits are many, small and continuous -- a pitch drag, a split, a
length. The host's undo cannot reach them, and an editor of this kind without
undo is unusable.

## Decision

- **A product with its own document keeps its own undo history** in the
  engine, as a stack of document versions.
  - A gesture (press, drag, release) is one step.
  - A command that changes many things at once is one step.
  - Selection is not undoable.
- **Undo and Redo are buttons** in the editor's top bar. Every function is
  reachable with the mouse, and each button has an info line.
- **Cmd-Z undoes and Shift-Cmd-Z redoes** (Ctrl on Windows and Linux), only
  while the editor has keyboard focus. Without focus the keys go to the host.
- **A further document key is an amendment to this decision**, kept on the
  same terms: only while the editor has keyboard focus, one step, undone like
  any other, and named beside its mouse route in the control's info line.
  Each one argues for itself, since a host's user loses that key while the
  window has the keyboard.
- **Every other host shortcut still goes to the host** -- Cmd, Cmd-Shift,
  Ctrl or Alt with a letter or a digit -- and Space always does. (The kit's
  controls answer Return and the arrows with or without modifiers, which no
  host binds while a plugin window has the keyboard.)
- **`tests/ui/hostkeys.h`** asserts both halves: `modifiedUsers` sweeps every
  modified letter and digit through a window, and an editor's test names the
  keys it keeps (`undoKey`, `redoKey` and its amendments). The kit's own
  sweep keeps none: no gallery page uses a modified key.

## Consequences

- **Two undo histories exist** in a set using such a product: the host's
  (parameters) and the plugin's (its document). The product's tutorial says
  which is which.
- **A host that grabs Cmd-Z before the plugin window sees it** leaves the
  buttons as the way to undo. The buttons are therefore not optional.
- **A new document key is an amendment here**, named in `hostkeys.h`, never a
  key an editor takes on its own.
