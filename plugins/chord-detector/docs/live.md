---
section: live
title: NI Chord-Detector in Ableton Live
---

## Where to put it

NI Chord-Detector is an **instrument**, because Live gives a plugin the notes of
a MIDI track only when it is an instrument. It makes no sound, so it goes where
it can hear the notes without taking the place of the synth that plays them.

**In an Instrument Rack, beside your synth.** This is the usual way.

1. Group your synth into an Instrument Rack: select it and press **⌘G**
   (**Ctrl+G** on Windows).
2. Open the rack's chain list and drag **NI Chord-Detector** into the empty
   space under the synth's chain. It becomes a second chain.
3. Play. Both chains get every note: the synth sounds, NI Chord-Detector names.

**On its own track.** To watch a track without touching it:

1. Make a new MIDI track and put NI Chord-Detector on it.
2. Set its **MIDI From** to the track you want to watch, and its monitoring to
   **In**.

## A first chord

1. Play C, E and G together. The readout says **C**, with **I** beside it,
   **C MAJOR** underneath and **C3 E3 G3** as the notes. On the circle C, E and G
   light up, C filled as the root.
2. Add A above G, so you hold C E G A with C at the bottom. The readout says
   **C6**, and under **Also** it lists **Am7/C**: the same four notes, read from
   A. Now play them with A at the bottom: **Am7**. The lowest note decides.
3. Play E G C, E lowest: **C/E**, *C major · 1st inversion*.

## Reading in a key

Your song is in D Dorian. Click **D** on the circle and choose **Dorian** in its
centre. The circle's signature says *no sharps or flats* — D Dorian uses the
white keys — and the seven tinted discs are its notes.

Now play C major: it reads **VII**, the seventh chord of the mode. Play B flat
major: **bVI**, a chord borrowed from outside the key, and named **Bb**, not A#,
because a score in D Dorian would write it that way.

## Hold and the pedal

Turn **Hold** on, play a chord and let go one key at a time. The name stays the
chord you played, and when the last key is up it stays on screen, dimmed, with
**HELD** lit. The next note you play replaces it.

The sustain pedal counts as holding: notes you release while it is down keep
sounding, and keep being named, until it comes up. The **PEDAL** light shows it
is down.

## Watching the history

The history scrolls with your song's bars. Switch between **Staff** and
**MIDI** at its top right, and choose how many bars with **Span**.

On the staff, a note outside the key signature carries its accidental, and a
natural cancels the signature where the key would raise or lower a note. Chord
names appear above the staff where each chord began.

## When it shows nothing

- **Nothing at all:** NI Chord-Detector is on an audio track, or the track is not
  monitoring. It needs MIDI: put it on a MIDI track, in an Instrument Rack, or
  set **MIDI From** as above.
- **It stops naming after Stop:** Live's Stop sends *All Notes Off*, and every
  held note — and a held chord — is cleared. That is the panic working.
