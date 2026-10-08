---
title: NI Trance Gate
tagline: A tempo-locked step gate, in Live and on the Move, from one Rust engine.
order: 1
hosts: [live, move]
formats: [VST3]
engine: engines/trance-gate
crates: [tg-core, tg-capi, tg-move]
tests: [tg_core, tg_render_ab, tg_curves, tg_envelope, tg_processor, tg_host]
still: media/trance-gate/live.png
---

# NI Trance Gate

A tempo-locked step gate: rhythmic chopping locked to song position, per-step
ADSR, ties, per-step amount, a fade-in that introduces the steps one at a time,
and 8 slots, each a complete sound. A VST3 for macOS (Linux and Windows follow) with a native
editor, and a Schwung module for the Ableton Move.

**It is the same engine in both**, and that is asserted rather than claimed.
`tg-capi` wraps the core in a C ABI for the plugin and `tg-move` wraps it in
Schwung's `audio_fx` vtable for the Move; both are members of one Cargo
workspace and both reach `tg-core` by relative path, so there is nothing to
keep in sync. `tests/render_plugin.c` then renders four seconds through the
plugin's own audio path — float, split channels, a DAW-shaped transport — and
the result is byte-for-byte identical to the module's reference render.

## Your pattern is part of the project

Everything you draw — which steps sound, the ties, each step's amount, the
arrival order, all eight slots — is saved with your Live set and comes back
when you reopen it. Draw a pattern, save, close Live, open the set again: the
pattern is the one you left, not the one the set was first loaded with.

## Slots: eight complete sounds

Each of the 8 slots remembers everything except the Slot choice itself: the
pattern (steps, ties, amounts, length, arrival order) and every control —
Rate, Amount, Width, Attack, Decay, Sustain, Release, Curve, Env Time, Join
Neighbors, Fade, Fade Shape and Fade Dir. Changing a control changes it only in
the slot you are on.

Switching slots recalls the whole sound, whether you pick the slot in the
editor, automate Slot in Live, or turn the Slot knob on the Move. In Live the
plugin's controls and automation lanes show the recalled values. The switch is
smooth: Amount and Sustain glide, the envelope carries on from where it is, and
the gate stays locked to the bar even when the new slot uses another Rate.

For example: put a tight 1/16 gate at 100 % Amount in slot 1 and a soft 1/8
swell at 40 % in slot 2, then automate Slot from 1 to 2 for the breakdown.

Projects and Move presets saved before slots held their own sound open with
their settings copied into all 8 slots, beside each slot's own pattern, so
they sound exactly as before.

## Slot files

**Export slot** saves the current slot to a `.nitgslot` file; **Export all**
saves all 8 slots to a `.nitgbank` file. **Import** opens either kind: a slot
file replaces the current slot, a bank replaces all 8. In Live they are icons
in the column right of the panels, and the hint bar names each one. The dialog remembers the
last folder you used, and the hint bar says what happened, for example
"Imported Bassline.nitgslot into slot 3."

A file that is not a Trance Gate slot file, or was written by a newer version,
is refused with the reason, and nothing changes. Imported slots are saved with
your Live set. The files are readable text, so they are easy to share. Slot
files are a plugin feature; the Move keeps its own presets.

## Patch interchange

**Copy and paste work on a slot.** The **copy** icon (*Copy slot*) puts the
current slot — its pattern and its whole sound — on the clipboard. The
**paste** icon (*Paste slot*) puts what is on the clipboard into the
current slot. The other slots are not touched.

For example, to start slot 2 from slot 1:

1. Select slot 1 and press **copy**. The hint bar says "Copied slot 1."
2. Select slot 2.
3. Press **paste**. The hint bar says "Pasted into slot 2.", and slot 2 now
   plays slot 1's pattern with slot 1's Rate, Amount, envelope and fade.
   Change it from there; slot 1 stays as it was.

The same works between two Trance Gates — copy on track 1, paste on track 2 —
and between sets. What is copied is the text of a slot file, so you can paste
it into a message or a note and paste it back later.

**Paste reads what is on the clipboard and acts on it:**

| on the clipboard | what paste does |
|---|---|
| a slot (copy, or the text of a `.nitgslot` file) | replaces the current slot |
| a bank (the text of a `.nitgbank` file) | replaces all 8 slots |
| a whole patch | replaces all 8 slots and every setting |

A *whole patch* is the plugin's saved state, which **is** the Move patch,
verbatim, and it is also what the copy icon put on the clipboard before
version v2026.10.06.2. So a patch from the Move, or one copied from an older
Trance Gate, still pastes — and still replaces every slot, as it always did.

Anything else on the clipboard is refused, and nothing changes: the hint bar
says "Failed to paste: The clipboard doesn't hold a Trance Gate slot."

The plugin reads and writes the clipboard itself, so copy and paste work inside
Live, where the host keeps **⌘C** and **⌘V** for its own menu.

## The fade

**Every step carries an arrival number**, and Fade introduces them in that order,
evenly spaced — the step ranked *r* of *n* arrives at exactly *r/n*.

**Out** (the Fade Dir parameter, **Dir** on the Move) chooses which end the
pattern is built up from. The knob means the same thing both ways: *how much of
the drawn pattern is present*. Only the missing part differs.

| | what arrives, one at a time | an un-arrived step | at 0% | at 100% |
|---|---|---|---|---|
| **Out** off (In) | the steps you drew ON | is a gap — silent | silence | the pattern |
| **Out** on | the steps you drew OFF, the holes | sounds, like an ordinary step | every hole filled | the pattern |

So 100% is the pattern either way, which is what makes it the neutral default and
lets the direction be switched at rest without changing a sample.

**Out fills holes; it does not bypass the gate.** At Out 0% every step sounds, and
below Width 100% the gate still pulses — a denser gate, not an open one. Amount is
what bypasses.

**Soft** ramps a step in on its own level, the same quantity a vertical drag in a
pad sets. **Hard** jumps it on. An arriving *hole* ramps the other way — it starts
as a full step and fades down to nothing, which is a gap. They are one formula and
a threshold:

```
w(r) = clamp(f·n − (r−1), 0, 1)     soft
w(r) = w_soft(r) ≥ 1 ? 1 : 0        hard
```

so the two agree at every arrival boundary — the switch changes a step's shape,
never *when* it arrives — and in soft mode exactly one step is ever part way in.

A step the fade has not reached is a **gap**, not a silent step that is on. The
difference is audible: ties and Join Neighbors ask the pattern whether a step
sounds in order to decide whether to hold a gate open through it, so a step that
has not arrived must not keep its neighbour's gate open.

**The order is the point.** In position order a fade can only be a left-to-right
wipe; shuffled, it is a build-up. **Randomize** shuffles it along with the pattern,
**Shuffle order** shuffles it alone, and **Set order** in the Fade panel lets you
tap the steps into the sequence you want — or
click a number on a pad and type one. Typing a number that is taken **swaps** the
two steps, so nothing between them moves.

The hits and the holes carry **separate orders**, ranked among themselves, because
a step is one or the other and never both. Fade Out sequences the holes.

## Randomize

**Randomize** fills the current slot with a Euclidean gate — a random number of hits
spread as evenly as the length allows, always with one on the downbeat — and a
shuffled arrival order. Sixteen coin flips reads as noise rather than as a trance
gate, which is why it is not that. Ties are cleared and the levels return to
full: they describe a pattern that no longer exists.

It does not touch the playhead, so it is safe to press mid-bar.

## What the host can automate, and what it cannot

Fifteen controls — Slot, Length, Rate, Amount, Width, the four envelope
stages, Join Neighbors, Env Time, Env Curve, and the fade's knob, shape and
direction — are ordinary host parameters. Automate them, and they behave the way a
DAW expects. **Fade is the one this matters most for**: a build-up is that knob
drawn across eight bars.

Every one of them except Slot belongs to the current slot. When the Slot
changes, the plugin moves each of them to the new slot's value and tells the
host it did, so the automation lanes and Live's own controls show the recalled
sound. Automating any of them writes into the slot that is playing.

**The pattern is not among them**, and that is a decision rather than an
omission. Which steps sound, which are tied, how loud each one is and when it
arrives would be 128 × 8 = 1,024 more parameters. So the pattern travels in the
saved state, which is the same model the Move module uses and the reason a
patch can cross between them at all.

## Timing

The gate follows song position, so it stays bar-aligned however long it runs
and survives a seek. Rate is one step's length: at 1/16 a 16-step pattern is one
bar, and a pattern can be up to 128 steps — eight bars at 1/16.

**The envelope stages are measured against the gate, not the clock.** Attack,
Decay and Release are a percentage of the step's open time — one step at the
current Rate, times Width — from 0 to 200 %. So the envelope keeps its shape when
the tempo changes: a Decay of 50 % is half the gate at 90 BPM and at 140 BPM.
**Env Time** only chooses how the readouts show it, as milliseconds at the
current tempo or as that percentage; the sound is the same either way.

**Changes are smooth.** Amount and Sustain glide to a new value over about
5 ms instead of jumping, so turning or automating them mid-note does not click.
Starting the transport picks up from the level the gate is already at, and
stopping it lets the gate open over the same few milliseconds — neither one
clicks any more.

With the transport stopped the gate holds open and audio passes.
