# Actions

Window-level verbs (Copy patch, Paste patch; also Randomize, Reset) grouped in one block at the top of the side column, right of the panels. Two forms, never mixed in one window:

- **With labels** (`ph-actions stack`): buttons stacked with an 8px (`space-2`) gap, all as wide as the widest label, icon then text, left-aligned. Use it when the side column has room (≥ 128px) or the plugin is new to its users.
- **Icons only** (`ph-actions joined`): 28px square icon buttons joined edge to edge, sharing one hairline, in the same order as the labelled form. Every button carries `aria-label` and `title` with the full verb ("Copy patch"). Use it in compact windows.

Layout: the side column (`ph-side`) stretches to the height of the panels. The actions sit flush with the panels' top edge, the transport and status (`foot`: transport group + Sync LED) flush with their bottom edge. Left edges of the actions and the transport align. Order is always copy before paste, left to right or top to bottom. Consumer provides: the actions (icon, label, onClick, disabled) and the variant.
