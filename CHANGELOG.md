# What's new

## v2026.10.05.1

### Before you update

- **Quit Live completely before you install.** Live keeps every plugin it has
  loaded in memory until it quits, so a bundle replaced while Live runs is not
  the one you hear. Install, start Live, then **Settings → Plug-Ins → Rescan**.
- **Update NI Listen-In and NI Spectrogram together.** The bus between them has
  a new format, and an old and a new build cannot share a slot.
- Every plugin bundle now carries its licence notices, `LICENSE` and
  `THIRD_PARTY_LICENSES.md`, in `Contents/Resources/`.

### In every editor

- **Double-click any knob, or any Side-Chain handle, to reset it** to the
  plugin's default. It is one ordinary edit, so undo takes it back.
- **Click a readout once to type a value.** Enter keeps it, Escape leaves the
  value as it was.
- **Everything works from the keyboard.** Tab to a control and use the arrows
  (Shift for fine), Page Up/Down, Home/End, and Enter to type.
- The window no longer repeats the plugin's name; the hint bar along the bottom
  carries the Motion switch and the Neon Ingvy mark.
- The animated background rings on bass onsets only, and its detector runs only
  while the window is open.

### NI Trance Gate

- **Your pattern is saved with the set.** Before, a set saved after editing the
  pattern could reopen with the pattern from before the edits.
- Copy gate config and Paste gate config move the whole state as one line of
  text, between tracks, sets and the Move.
- Typing a stage time follows the Time setting: with Time on ms, `40` means
  40 ms. A unit you type — `40 ms` or `25 %` — always wins.
- Starting and stopping the transport no longer clicks.
- Amount and Sustain glide over about 5 ms instead of jumping.
- The envelope plot under the ring is the engine's own rendering of the patch.
- Keyboard: arrows move between pads, Space or Enter toggles a step (Shift for a
  tie), Alt + ↑/↓ sets its amount; the ring is the Length control.
- On the Move, the module's audio is rounded rather than truncated to 16 bits,
  so quiet gated passages keep their detail.

### NI Side-Chain

- MIDI notes duck only when Source is MIDI. A kick pad on the same track no
  longer fires extra ducks on the Cycle or Sidechain source.
- Depth glides, and a stage length or tempo changed in the middle of a duck
  carries on smoothly. Stopping the transport on Cycle releases the duck
  instead of cutting it.
- The key input is on inputs 3 and 4, with a stereo or mono main; in CLAP a key
  needs a stereo main. The manual walks through routing a key in Live.
- The shaper handles take the keyboard: ←/→ along the cycle, ↑/↓ for Depth on
  the bottom corner.
- The header warns about a silent track (`no input`) and a key that never
  crosses Threshold (`no trigger`) as well.
- On the Move, the trigger note is a note name, defaults to C1 (MIDI 36), and
  stays on C1 when you reset it.

### NI Spectrogram

- **It keeps its view, comparison, clash marking and zoom** when you close and
  reopen the window, and when you reload the set.
- The analysis runs on a thread of its own instead of on Live's audio thread.
- Reading Listen-In buses works on Intel Macs.
- A Listen-In that is removed and comes back is found again within a second.
- A bus at another sample rate is shown greyed out with its rate, and is never
  drawn.

### NI Listen-In

- A bus slot belongs to one Listen-In at a time; a second one on the same
  number says `slot taken` instead of sharing it.
- After a Live restart, or even a crash, every Listen-In claims its saved bus
  again.
- New bus format: update together with NI Spectrogram, after quitting Live.
