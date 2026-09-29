---
title: Listen-In
tagline: A tap on any track — passes audio through, publishes it on a numbered bus.
order: 3
hosts: [live]
formats: [VST3, AU, CLAP]
engine: engines/audio-bus
crates: [bus-core, bus-capi]
tests: [abus_core, abus_roundtrip, abus_ipc, listenin_wire, listenin_wire_js]
notOnMove: >-
  Listen-In is not a DSP — it is a transport between two plugins in one host,
  and the Move runs one module at a time with no second plugin to read the bus.
  So there is no `bus-move` crate and `cmake/Schwung.cmake` looks for none.
---

# Listen-In

Drop it on a track, or into an instrument rack's chain. Audio passes through
**bit for bit** and a copy is published on one of **sixteen numbered buses**,
where any other plugin can read it — in this process or another.

It makes no sound, draws no picture and adds **no latency**. Its whole output is
somewhere else.

## Why it exists

The Spectrogram analyses the track it sits on and nothing else. To compare two
sources — to see where a bass and a pad are fighting over the same 200 Hz — you
would need two Spectrograms on two tracks, each drawing its own picture, and
your eyes doing the overlay. What you want is both spectra in **one** picture,
and that needs one analyzer able to read audio from somewhere it is not.

This is that somewhere. A Listen-In on the bass and another on the pad, and a
receiver can draw both.

## Two controls

**Bus** is a host parameter — automatable, saved with the set, and restored when
you reopen it. **Name** is not: a name is not a number, so it travels as a
message and the plugin serialises it into the state chunk itself. Sixteen buses,
because a number you can hold in your head beats a picker you have to read.

**Two Listen-Ins cannot share a bus**, and the second one says so rather than
quietly publishing nothing. That is the one collision this design can have and
it is reported, never swallowed.

## The transport is shared memory, and that is the whole design decision

A process-wide table inside the shared library would have been two hundred
lines and no system calls. It would also have been dead the first time a host
put the sender and the receiver in different processes — an AU under a sandbox,
a bridged VST3, a plugin scanned out of process — and the symptom would have
been a receiver that is simply always empty, with nothing in any log.

So each bus is a POSIX shared-memory segment: `/nia.bus.NN`, a header and a ring
of 131072 stereo frames — 1 MiB, of which a reader may use 122880 (2.56 seconds
at 48 kHz); the remaining 8192 are the margin it keeps between itself and the
writer, for the reason below. One writer, any number of readers, and **no
coordination between the readers at all** — each keeps its cursor in its own
memory and the segment is written by exactly one participant.

`abus_writer_push` runs on the audio thread and allocates nothing, locks nothing
and makes no system call; `cargo test -p bus-core` fails the build if that stops
being true.

**Falling behind is reported, not papered over.** A receiver that stalls comes
back to find the writer has lapped it, and `abus_reader_read` tells it how many
frames went past instead of handing it a buffer spliced from two different
moments. A splice looks exactly like audio. It is the failure this design was
most likely to have, so `a_reader_is_never_handed_a_splice` runs a writer flat
out against a deliberately slow reader and checks every delivered block for
internal continuity — and it caught one, about one run in three, until the
reader was given a margin to stay behind. `engines/audio-bus/README.md` has
the whole story; the short version is that a writer which has copied its samples
but not yet published the count is invisible to any amount of re-checking.

**A crashed host leaves the segment behind** — shm outlives its process, to the
next reboot. A claimer reclaims a slot only when the heartbeat has stopped *and*
`kill(pid, 0)` says the process is gone; either alone lies, because pids are
recycled and a merely-paused host still holds its slot.

## One known limit

If a host sandboxes the plugin, POSIX shm names need an app-group prefix and
this will not connect — the editor says `unavailable` rather than pretending.
Live loads VST3 and AU in process, which is what it was built for.

## Seeing it work without a receiver

`abus_tap` is built with the tests and is the answer to "is it actually
routing?":

```
abus_tap        # every slot, what is on it, and what it is called
abus_tap 3      # follow slot 3: peak, frames, frames dropped
```

It is also the worked example of the receiving ABI, in sixty lines. Whoever
wires the Spectrogram to a bus starts there and at
`engines/audio-bus/include/audio_bus.h`.
