# Panel

A hairlined `bg-100` box that groups the controls of one function (Gate, Envelope) inside a `bg-000` window. Use one panel per function, never one per control; a window with a single function has no panel at all, just the window. Consumer provides: the `title` and the controls as children. Padding is `space-4`, panels sit `space-6` apart. Do not nest panels, do not give a panel a heading longer than two words.
