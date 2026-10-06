# Actions

Window-level verbs grouped in one block at the top of the side column, right of the panels: the clipboard pair (copy, paste) and the file verbs (export, export all, import), which in the Trance Gate act on a slot or on the whole bank of slots; a window that has Randomize or Reset puts them here too, after the others. Two forms, never mixed in one window:

- **With labels** (`ph-actions stack`): buttons stacked with an 8px (`space-2`) gap, all as wide as the widest label, icon then text, left-aligned ("Copy slot", "Paste slot", "Export slot", "Export all", "Import"). Use it when the side column has room (≥ 128px) or the plugin is new to its users.
- **Icons only** (`ph-actions joined`): 28px square icon buttons joined edge to edge, sharing one hairline, in the same order as the labelled form; five verbs take 136px. Every button carries `aria-label` with the full verb ("Copy slot") and its info string ("Copy slot — put this slot, pattern and sound, on the clipboard."), or a `title` where it has none. Use it in compact windows.

Order is always copy, paste, export, export all, import, left to right or top to bottom. Export saves the current item (a slot) to a file; export all saves every slot, a bank, to one file; import loads a file, and the file decides what it replaces (a slot file the current slot, a bank file every slot), so import needs no choice of its own. The plugin opens the system's file panel, and how the action went comes back as an outcome in the `Hint` bar ("Copied slot 1.").

Layout: the side column (`ph-side`) stretches to the height of the panels. The actions sit flush with the panels' top edge, the transport and status (`foot`: transport group + Sync LED) flush with their bottom edge. Left edges of the actions and the transport align. Consumer provides: the actions (icon, label, info, onClick, disabled) and the variant.
