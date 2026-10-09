# What's new

## v2026.10.09.2 — NI Side-Chain, NI Listen-In, NI Spectrogram

### Before you update

- **Update NI Side-Chain, NI Listen-In and NI Spectrogram together**, and quit
  Live fully first. The Listen-In bus now carries Live's timeline, so a plugin
  from before this release and one from this release do not see each other's
  buses. Sets open unchanged.

### NI Side-Chain

- **See the kick you duck against.** A new **Kick** picker, at the right of
  the shape's row, draws the kick behind the duck in amber: from the sidechain
  key Live routes to the plugin, or from an NI Listen-In on the kick's track.
  The grey input and the violet output are drawn over it as before, so you can
  set Delay, Attack, Hold and Release against the kick's transient itself
  instead of the millisecond ruler. It is a picture only — what you hear is
  unchanged — and the choice is saved with the set.
- **Exact to the sample.** The kick is drawn where each of its samples
  sounded in the cycle, whichever track Live processed first. With the
  transport stopped it is drawn as it arrives, and the caption says
  `KICK BY ARRIVAL`.

### NI Listen-In

- **Every block is stamped with its place on Live's timeline**, so a reader on
  another track can line it up with its own audio to the sample. NI Side-Chain
  is the first to read a bus this way.

### NI Spectrogram

- Reads the new bus format; nothing else changes.

## v2026.10.10.1 — NI Side-Chain

- **NI Side-Chain on Ableton Move keeps its settings.** Schwung now saves the
  Side-Chain's settings with the set and restores them when the set loads.
  Until now it never could, and the other modules in the same slot weren't
  saved either. Load a set saved before this update once more and set the
  Side-Chain up again; from then on its settings are kept.
- NI Side-Chain's MIDI trigger is now entirely our own code. It behaves
  exactly as before.

## v2026.10.09.1

- **Space always reaches Live.** In every NI plugin window, Space starts and
  stops Live's transport, and Shift+Space continues play, wherever you last
  clicked. The only exception is typing in a text field, where Space is a
  space. Buttons, switches, menus and pads are pressed with Enter now
  (Shift+Enter sets a tie on a Trance Gate pad).
- **The signal plots zoom with your material.** In NI Side-Chain and the
  Trance Gate's Signal tab the audio fills the plot whatever the track's
  level: the range rises quickly with a louder passage, holds for two seconds
  and then eases down, and stays still on steady material. The range is shown
  over the top edge ("-21 dB"); double-click the plot for a fixed full-scale
  view. The shapes and the gate outline keep their true scale.
- **NI Trance Gate on Ableton Move: every step up to 128 is reachable.** The
  left and right arrows page the pads in blocks of 32 ("33-64 of 96"); the
  arrows no longer change the slot - Slot stays on the first knob.

## v2026.10.08.3 — NI Trance Gate, NI Side-Chain, NI Chord-Detector

- **NI Trance Gate: a set saved right after a Slot change keeps the right
  sound.** If Live saved in the moment between a Slot change during playback
  and the plugin following it, the new slot could reopen with the previous
  slot's sound. A save now always holds exactly what is playing.
- **NI Side-Chain and NI Chord-Detector** are unchanged; this is their first
  published macOS release, which v2026.10.08.2's build did not reach.

## v2026.10.08.2

Every plugin is rebuilt on a new foundation. Your sets open as before; what
changes is how the plugins are made, what they are licensed under and which
formats they come in.

v2026.10.08.1 was never published: its releases stopped on GitHub's build
machines before they built anything. This is that release, with the fixes that
get it built, the Move modules' among them.

### Before you update

- **VST3 only, for macOS** (universal: Apple silicon and Intel). Linux and
  Windows follow in a later release. The Audio Unit and CLAP versions are
  discontinued. Quit Live, then delete the old bundles so a rescan does not
  find them:

  ```sh
  cd ~/Library/Audio/Plug-Ins
  rm -rf Components/NITranceGate.component Components/NISideChain.component \
         Components/NISpectrogram.component Components/NIListenIn.component \
         CLAP/NITranceGate.clap CLAP/NISideChain.clap \
         CLAP/NISpectrogram.clap CLAP/NIListenIn.clap \
         Components/TranceGate.component CLAP/TranceGate.clap
  ```

  (The last two are the Trance Gate's names from before v2026.09.29.1.)

  A set that used the **Audio Unit** opens with the device missing: insert the
  VST3 in its place. A set that used the VST3 needs nothing.
- **Old Live sets open unchanged.** Every plugin keeps the class ID, the
  parameters and the saved state of its earlier VST3, so patterns, settings and
  automation come back as they were saved.
- Install as always: quit Live, copy the `.vst3` into
  `~/Library/Audio/Plug-Ins/VST3`, start Live and rescan (**Settings →
  Plug-Ins → Rescan**). Each plugin's manual has the details.

### Native editors on Ultraviolet 1.1.0

Every window is drawn natively, on the published Ultraviolet 1.1.0 design
system, instead of as a web page inside the plugin. The editors look and behave
as before: the same controls, the same keyboard use and the same hint bar.

### New: NI Chord-Detector

The fifth plugin names the chord a MIDI track plays: on a circle of fifths, as a
roman numeral in your key, on a keyboard and on a scrolling staff. See its
manual, and the v2026.10.07.2 entry below.

### Known differences

- **Bypass.** Each plugin's own Bypass parameter has a new ID: 15 in the Trance
  Gate and the Side-Chain, 0 in the Spectrogram, 1 in Listen-In (it was 65536).
  Live treats it as the plugin's bypass either way and a set's bypass state
  comes back, but an automation lane drawn on that Bypass wants drawing again.
- **Side-Chain MIDI-CC automation.** The earlier Side-Chain listed 128 MIDI
  controllers, aftertouch and pitch bend as parameters of their own (IDs 65538
  to 65667). They are gone — the plugin takes MIDI as MIDI — so an automation
  lane drawn on one of them finds nothing. The plugin never read them, and the
  panic (CC 120, CC 123) works as before.
- **Live's generic panel** may show list parameters (Source, Rate, Curve and the
  like) as stepped sliders rather than menus. They step through the same values
  and show the same words.
- **Short names** — where Live shows a parameter's short name, it is the first
  eight characters of its name.

### Under the hood

- **GPL-3.0-or-later.** The plugins are free software under the GNU General
  Public License, version 3 or later, and every release links the source it was
  built from. Each bundle carries the licence and every third-party notice.
- **The Rust core is built on established crates** — realfft, rtrb,
  triple_buffer, basedrop, wmidi, serde_json and others, each under a licence
  that combines with the GPL — instead of hand-written replacements.
- **Every bundle is tested before it is released**: in a host, and by
  pluginval and Steinberg's validator.

## v2026.10.07.2

### New: NI Chord-Detector

A new plugin that tells you what you are playing. Put it on a MIDI track — in an
Instrument Rack beside your synth — and it names the chord, its roman numeral in
your key, the notes, and the other names the same notes go by.

- **A circle of fifths** shows the key you chose, its seven notes, and the
  notes you are playing lit up. Click a key to choose it, and pick one of the
  seven modes in its centre.
- **Notes are spelled the way your key writes them**: B flat in C major, E sharp
  in F sharp major. Or choose sharps or flats throughout.
- **Inversions and slash chords** are read from the lowest note: `C/E`,
  `Am7/G`.
- **Hold** keeps the last chord on screen after you let go.
- **A scrolling history** of the last bars, as notes on a grand staff or as one
  line per MIDI note.
- **A keyboard** with the keys you hold lit.

It makes no sound, works with the sustain pedal, and clears at once on Stop.
It is a VST3 for macOS and the first plugin built on JUCE 9 with a native
editor; the others follow.

### Install

Quit Live, copy `NIChordDetector.vst3` into your VST3 folder
(`~/Library/Audio/Plug-Ins/VST3`), start Live and rescan
(**Settings → Plug-Ins → Rescan**). It is listed as an instrument from Neon
Ingvy.

## v2026.10.06.5

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
- Text and icons on a lit control — a pressed button, the selected tab — are
  deep violet on the white fill rather than black, so a lit control keeps its
  colour. The Trance Gate's numbers on lit steps and the marks on its envelope
  plot follow.
- **The animated background keeps time with the song.** It pulses on every
  beat while the transport plays, stronger on the first beat of each bar, and
  is still while stopped. It follows the tempo and time signature rather than
  the audio, so every plugin pulses alike, even on a silent track; it runs only
  while the window is open. It also moves in Live now: it no longer waits on
  screen refreshes that a plugin window hardly gets.
- **The Trance Gate's and the Side-Chain's playheads move smoothly in Live**,
  and dragging across the Trance Gate's pads is heard while you drag, not only
  when you let go. Both now run on timers rather than on those same screen
  refreshes.
- **Loading a preset or reopening a set with the editor open is safe.** The
  plugin no longer writes into its window from the thread Live loads on; the
  knobs and readouts catch up on the window's next tick, a moment later.

### NI Trance Gate

- **Every slot is a complete sound.** Rate, Amount, Width, the envelope,
  Curve, Env Time, Join Neighbors and the fade now belong to each slot, beside
  its pattern. Switching slots — in the editor, by automation or on the Move —
  recalls all of them, smoothly. Older sets open with their settings copied
  into all 8 slots, so they sound as before.
- **Export and import slots.** EXPORT saves the current slot (`.nitgslot`),
  EXPORT ALL saves all 8 (`.nitgbank`), IMPORT loads either back.
- **Your pattern is saved with the set.** Before, a set saved after editing the
  pattern could reopen with the pattern from before the edits.
- **Copy and paste a slot.** *Copy slot* puts the current slot, pattern and
  sound, on the clipboard; select another slot and *Paste into slot* to
  replace it. The plugin reads and writes the clipboard itself, so both work in
  Live, which keeps ⌘C and ⌘V for its own menu. A whole patch on the clipboard
  — a Move patch, or one copied from an older version — still pastes and
  replaces all 8 slots. Anything else is refused, and the hint bar says why.
- Pressing paste no longer opens a field that pushed the editor sideways and
  cut off its left edge. No editor window can scroll any more.
- Typing a stage time follows the Time setting: with Time on ms, `40` means
  40 ms. A unit you type — `40 ms` or `25 %` — always wins.
- Starting and stopping the transport no longer clicks.
- Amount and Sustain glide over about 5 ms instead of jumping.
- The envelope plot under the ring is the engine's own rendering of the patch.
- Keyboard: arrows move between pads, Space or Enter toggles a step (Shift for a
  tie), Alt + ↑/↓ sets its amount; the ring is the Length control.
- **Length holds on whole bars.** Dragging the Length knob stops briefly at
  half a bar, one, two and four bars at the current Rate and Live's time
  signature — 16, 32, 64 and 128 at 1/32 in 4/4. Small marks on the knob show
  where they are. Page Up/Down jump between them, and Shift-drag passes
  through.
- **Every control says what it does.** Hover over any control, or Tab to it,
  and the hint bar at the bottom says what it does in one line — "Rate — the
  length of one step, synced to the song tempo." Move away and the bar shows
  its usual tips again. The same line is what a screen reader announces.
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
- The picture can no longer stay blank for a whole session when the window
  opens on a busy computer.

### NI Listen-In

- A new Listen-In's name field is empty, waiting for a name, instead of
  reading `(null)`.
- A bus slot belongs to one Listen-In at a time; a second one on the same
  number says `slot taken` instead of sharing it.
- After a Live restart, or even a crash, every Listen-In claims its saved bus
  again.
- New bus format: update together with NI Spectrogram, after quitting Live.
