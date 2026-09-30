/*
 * The second row: what the picture is OF, and what the orange is measuring.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * TWO GROUPS, ONE STRIP, AND A RULE BETWEEN THEM. "What is the picture of" and
 * "what is the clash measuring" are different questions, and one undivided row
 * made them read as one tangled setting. The rule costs no height.
 */
import { CheckList, Select, Toggle } from '@ultraviolet/ui';

export function SourceStrip(props) {
  return (
    <div class="control-strip">
      <div class="control-group">
        <span class="group-label t-label">view</span>
        {/* Several channels, ADDED. A CheckList rather than a Select, because a
          * native <select multiple> stops being an OS popup -- see its header. */}
        <CheckList
          summary={props.summary}
          width={150}
          emptyText="no Listen-In found"
          selected={props.view}
          onChange={props.onView}
          options={props.options}
        />
      </div>

      <i class="control-divider" />

      <div class="control-group">
        <span class="group-label t-label">compare</span>
        <Select ariaLabel="Compare" options={props.names} value={props.cmpA}
                onChange={(i) => props.onCompare('a', i)} width={112} />
        <span class="vs t-hint">vs</span>
        <Select ariaLabel="Against" options={props.names} value={props.cmpB}
                onChange={(i) => props.onCompare('b', i)} width={112} />
        <Toggle label="clash" value={props.clash} onChange={props.onClash} />
      </div>
    </div>
  );
}
