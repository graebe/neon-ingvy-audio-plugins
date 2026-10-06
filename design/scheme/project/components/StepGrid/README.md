# StepGrid

Sixteen `Step`s per row at `space-2` gaps, one row per lane; the first step of every beat gets a `line-200` border so bars read without numbers. A pattern `Select` sits above-left. The grid is 760px wide at 16 steps and never scales: a window with a grid is at least that wide. Consumer provides: rows as arrays of step states, the playhead index, the pattern list. More than 32 steps become pages, not a smaller step.
