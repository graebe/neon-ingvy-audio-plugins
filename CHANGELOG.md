# What's new

## Unreleased — every plugin

These are already in the shared plugin shell and reach each plugin with its
next release; that release's entry lists them and this section goes.

- **The background is still.** The animated background that pulsed with the
  beat is gone, and the **Motion** switch in the hint bar with it. The window
  ground is the same violet dot paper and grain, always at rest; the hint bar
  carries the tips and the Neon Ingvy mark.
- The Motion setting each plugin remembered on your computer is simply ignored
  now; there is nothing to reset.

## Unreleased — NI High Pass, NI Low Pass, NI Band Pass, NI Notch, NI Bell, NI Low Shelf, NI High Shelf, NI Tilt, NI All Pass

- **New: nine single filters.** One NI Filter Designer filter each, as a
  plugin of its own, for dragging just that filter into a chain: **NI High
  Pass**, **NI Low Pass**, **NI Band Pass**, **NI Notch**, **NI Bell**, **NI
  Low Shelf**, **NI High Shelf**, **NI Tilt** and **NI All Pass**. Each sounds,
  sample for sample, as that filter does in NI Filter Designer.
- **Only the controls a filter has.** A cut has Freq, Q, Slope (6 to 48
  dB/oct), the six approximations with Ripple and Stopband, and Matched; a
  bell Freq, Gain, Q and Adaptive; a shelf Freq, Gain, Q and Slope; a tilt
  Freq and Gain; an all-pass Freq, Q and Order. Every one has Channel
  (Stereo, Left, Right, Mid, Side) and Output, always in the same place.
- **One picture**: the filter's curve over a pre/post analyzer of the lane it
  works on; drag the dot for frequency and gain (or Q), Alt-drag a gain
  filter's dot for Q, drag a bell's or band's edge for its width. NI All Pass
  draws its phase.
- **On the Move** too: each a Schwung module with the plugin's parameters on
  one page of knobs, frequency and Q in musical steps, and the panic.

## Unreleased — NI Filter Designer

- **New: NI Filter Designer.** Up to sixteen minimum-phase IIR filters on one
  response curve: bell, low shelf, high shelf, high-pass, low-pass,
  band-pass, notch (band-stop), all-pass and tilt. Double-click the curve to
  add a filter, drag its handle for frequency and gain, Alt-drag for Q, Delete
  removes it. Every edit is one undo step in Live.
- **Six approximations** for high-pass, low-pass, band-pass and notch —
  Butterworth, Bessel, Chebyshev I and II, elliptic and Linkwitz-Riley — at
  orders 1 to 8 (6 to 48 dB/oct), with Q on the highest-Q pole pair, passband
  ripple and stopband attenuation. A change of family moves the filter into
  the new one over 20 ms; a change of type or order crossfades over 20 ms to
  a filter that has already settled on the music, so even a narrow, low one
  comes in at its level instead of swelling up from silence.
- **A three-band isolator** in front of the filters: Linkwitz-Riley
  crossovers (LR4 or LR8), a level and a kill per band.
- **Per-filter channel**: stereo, left, right, mid or side. The total curve
  is exact in the selected filter's lane, filters on other lanes included,
  in the order they play: a mid bell shows in the LEFT lane as a source
  panned hard left hears it.
- **Spectrum analyzer** behind the curve: the input (pre) faded, the output
  (post) highlighted, in the selected filter's lane (the side of a side
  filter). The curve is the engine's own transfer function.
- **Bypass**, crossfaded in 10 ms, the filters kept running for two seconds
  so they resume from the signal they heard; after that the plugin idles and
  costs nothing. No latency.
- **Matched cuts**: an option to match every high-pass and low-pass to its
  analog prototype, so filters near Nyquist keep the analog slope and peak.
  Switching it moves the cuts over 20 ms.
- **The tail** reported to the host: how long the filters ring after the
  input stops, to -60 dB (a Q 40 notch at 20 Hz: 4.4 s).
- **Smooth automation**: Freq, Gain and Q move the filters a sample at a
  time, and so do the isolator's crossovers, so sweeping a narrow filter or a
  crossover leaves no stepping artefacts. Where a sweep of a shelf or a
  matched cut high in the band passes from one way of building it to
  another, the change is spread over 20 ms instead of clicking.
- **Bounded cost on the audio thread**: a preset load or automated Ripple and
  Stopband design at most four prototypes per 16 samples, and sweeps of
  shelves and matched cuts high in the band at most four of their costlier
  designs; a 64-sample block after a preset turns all sixteen filters into
  order-8 elliptic low-passes costs about 50 µs (4 % of its real time at 48
  kHz), sixteen matched order-8 elliptic cuts swept at once about 86 µs.
- On the Ableton Move as a Schwung module: the isolator, a high-pass and a
  low-pass.

## Unreleased — NI Vocalize

- **New: NI Vocalize.** Turns any sound, step by step, into a voice singing
  A, E, I, O or U. **Vocal** says how far: at 0 % the sound is untouched, at
  100 % it sings the vowel, and everything between is the sound on its way
  there. **Vowel** sweeps smoothly from one vowel to the next, **Size** makes
  the voice bigger or smaller, **Resonance** broad or whistling, and
  **Excite** gives pure sounds like a sine bass something to sing with.
- Two engines: **Time** has no latency and sounds like a talkbox;
  **Spectral** is smoother and more sung, and about 21 ms late, which Live
  compensates for.
- **Your own voice.** Drag a recording of yourself singing A, E, I, O, U onto
  the window, or click **Capture…**: Vocalize learns your vowels and sings
  with them. **Voice** morphs from the textbook voice to yours. The captured
  voice is saved with the set; the recording is not needed afterwards. Sung
  A-E-I-O-U in order, each vowel is taken by its place; the chart shows how
  sure each one is, and **Restore** brings back a voice forgotten by mistake.
- **Ceiling**, on by default: nothing that leaves Vocalize passes full scale.
- On the Ableton Move as a Schwung module, with the Time engine.

## Unreleased — NI Spectrogram

- **Beat lines.** A new **Grid** select between Span and Pause draws vertical
  lines over the picture at Live's grid: every bar, or every 1/2 down to
  every 1/128 note, counted from each bar's start. Bars are drawn clearest,
  beats lighter and finer notes faintest. The lines scroll with the history
  in seconds and sit on the ticks in the bar view. Changing the rate redraws
  the whole picture at once. The grid is saved with the set. Older
  sets open with no grid, and older builds still open sets saved with one.
- **A wider window, sixteen seconds of history.** The window grows from 720
  to 868 pixels so the Grid fits beside the picture's facts without either
  being cut. The picture grows with it and now holds sixteen seconds instead
  of thirteen.

## Unreleased — NI Bit-Crusher

- **Amplitude Crush and Time Crush.** The two panels are named for what they
  do: the first makes the signal's amplitude discrete (Bits, Shape, Dither),
  the second makes time discrete (Rate, Jitter and the filters).
- **Three pictures, one over each panel.** A new **Time Crush** picture shows
  the hold itself: your signal in grey, the held level as a dim line, each
  take lit as a dot, sixteen holds wide. The **Amplitude Crush** picture now
  shows your signal live, lighting up the stairs it is on. It is drawn on the
  level going into the crusher, after Drive, so its diagonal always runs
  corner to corner and Drive moves your signal up the stairs.
- **The pictures say exactly what you hear.** The Amplitude Crush caption
  counts the levels (257 at 8 bits, 256 with Mid-Rise: the level at zero is
  the one more); its dots only ever sit on the staircase that made them, and
  come back a moment after a knob moves; with Anti-Alias the Time Crush
  picture marks each take at its exact instant; and with nothing held it
  draws no hold.
- **Rulers on every picture.** Level and time are marked on the axes, and the
  two discretizations show their grid as ticks: every step of the staircase,
  every take of the hold.
- **New: a Filter.** A resonant low-pass on the crushed sound, before Mix,
  in a fourth panel with its own picture: **Cutoff** (20 Hz to 20 kHz),
  **Peak** (up to +24 dB at the cutoff, never self-oscillating) and **Slope**
  (12, 24 or 48 dB per octave), true to its analog shape up to 20 kHz. Off
  by default, so existing sets sound as they did. Every change glides, the
  slope included. Drag the dot on its curve: sideways for Cutoff, up and
  down for Peak. On the Move it is the second knob page.
- **The Amplitude Crush picture shows the whole range.** It no longer zooms
  in: the staircase is drawn from −1 to 1 at every setting, plain at a few
  bits and as fine as the diagonal at many.
- **Refresh and Pause.** The pictures take a new look once a second by
  default, all three at the same moment; **Refresh** sets 1/s, 4/s, 10/s or
  Live and is saved with the set (it is not automatable). **Pause** holds all
  three still while the sound goes on.

## Unreleased — NI Trance Gate

- **NI Trance Gate's editor, in your browser, for the Move.** Open Schwung's
  web manager (**move.local:7700** › **Remote UI**) and choose the tab of the
  track the Trance Gate is on: the module now shows its own editor there, in
  place of the generic controls — the ring, the pads, the envelope and
  pattern plots, Slot, Randomize and the arrival order, laid out as in Live.
  Click a pad, drag a knob or recall a slot, on the page or on the Move, and
  the other follows. **Interface: Default** switches back to the generic
  controls.
- **Its playhead stays in time.** The ring and the lit pad step with the Move
  however late the network delivers each update; they never step back, and
  never skip ahead as the pattern starts over.
- Live's Signal plot, copy and paste, and export and import are not in the
  Move's editor: the web manager sends a page no audio, and its frame has no
  clipboard and cannot save or open files.
- **A panic shuts the gate on the Move.** CC 120 or CC 123 closes it at once,
  and the step under the playhead strikes again from silence. Before, the
  module ignored a panic.

## Unreleased — NI Side-Chain

- The shape's handles are the plugin family's shared draggable handle now.
  They look and drag exactly as before; the bottom corner, which moves both
  ways, shows a four-way cursor while you hover it.
- **NI Side-Chain's editor, in your browser, for the Move.** Open Schwung's
  web manager (**move.local:7700** › **Remote UI**) and choose the tab of the
  track the Side-Chain is on: the module now shows its own editor there —
  the duck shape to drag, the knobs, Source, Rate, Curve and the MIDI trigger,
  as in Live. Changes go both ways between the page and the Move.
- **Its sweep stays in time.** The playhead runs through the shape with the
  Move, evenly, however late the network delivers each update, and runs on
  smoothly where each cycle starts over.
- The Move's editor draws no audio behind the shape (the web manager sends a
  page no audio), and offers Cycle and MIDI, as the Move module does.

## Unreleased — NI Chord-Detector

These are already in the shared plugin shell and reach NI Chord-Detector with its
next release; that release's entry lists them and this section goes.

- **Bypass fades.** Switching Bypass on or off crossfades over 10 ms instead
  of jumping.
- **Loops are exact.** When Live's loop wraps inside a buffer, it continues
  from the loop's start on that very sample.

## v2026.10.11.1 — NI Bit-Crusher

- **New: NI Bit-Crusher.** A bit crusher for Live and the Move: it keeps fewer
  levels (Bits, 1 to 16, in fractions too, so a sweep glides) and keeps them
  less often (Rate, 50 Hz to 96 kHz). Drive, Shape (µ-law steps), Dither,
  Mid-Rise shape the steps; Jitter and the Pre and Post filters shape the
  hold. Anti-Alias draws the crusher exactly, so the same settings sound the
  same at every sample rate. Its window shows the staircase and the sound
  before and after. Every control can be automated without clicks. The
  plugin runs 32 samples (0.7 ms) late, which Live compensates. On the Move:
  one page of eight knobs.

## v2026.10.10.2 — NI Side-Chain, NI Listen-In, NI Spectrogram

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
- **Bypass fades.** Switching Bypass on or off crossfades over 10 ms instead
  of jumping.
- **Loops are exact.** When Live's loop wraps inside a buffer, the duck — and
  the kick drawn behind it — continue from the loop's start on that very
  sample.

### NI Listen-In

- **Every block is stamped with its place on Live's timeline**, so a reader on
  another track can line it up with its own audio to the sample — also across
  a loop that wraps inside a buffer. NI Side-Chain is the first to read a bus
  this way.
- Its Bypass still switches at once: it passes audio through untouched, so
  there is nothing to fade.

### NI Spectrogram

- Reads the new bus format. Its Bypass still switches at once, for Listen-In's
  reason.

## v2026.10.10.1 — NI Trance Gate

- **No more clicks from the gate itself.** No edge of the gate is shorter
  than 1 ms, so an Attack, Decay or Release of 0 %, or a very fast rate, no
  longer jumps the volume in one sample. The slow rates sound exactly as
  before.
- **A step at level 0 is silent, without a click.** Dragging a step's level to
  the bottom now closes the gate there like an off step, and the next step
  opens again. Switching a step on brings it back at full level, in the plugin
  and on Move alike.
- **Tighter and cleaner edges.** Every edge starts and ends exactly where it
  falls, between samples, so the gate sounds the same at any buffer size and
  its edges are about 29 dB cleaner.
- **Bypass fades.** Switching the Trance Gate's Bypass on or off crossfades
  over 10 ms instead of jumping.
- **Loops are exact.** When Live's loop wraps inside a buffer, the gate starts
  again at the loop's start on that very sample.
- **NI Trance Gate on Move** needs Schwung 1.5.0 or newer; paging past step 32
  with the arrows needs a Schwung newer than 1.7.3.

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
