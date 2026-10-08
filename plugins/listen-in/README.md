---
title: NI Listen-In
tagline: A tap on any track — passes audio through, publishes it on a numbered bus.
order: 3
hosts: [live]
formats: [VST3]
engine: engines/audio-bus
crates: [bus-core, bus-capi]
tests: [abus_core, abus_roundtrip, abus_ipc, li_processor, li_host]
notOnMove: >-
  NI Listen-In is not a DSP — it is a transport between two plugins in one host,
  and the Move runs one module at a time with no second plugin to read the bus.
  So there is no `bus-move` crate and `cmake/Schwung.cmake` looks for none.
---

# NI Listen-In

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

This is that somewhere. An NI Listen-In on the bass and another on the pad, and a
receiver can draw both.

## Two controls

**Bus** picks one of **sixteen numbered slots**, 1 to 16. It is a host
parameter — automatable, saved with the set, and restored when you reopen it.
**Name** is what a reader such as NI Spectrogram lists the bus as: up to 31
bytes, saved with the set too, though not as a parameter — a name is not a
number. Sixteen buses, because a number you can hold in your head beats a
picker you have to read.

**A slot belongs to one NI Listen-In at a time.** The first to take a number
holds it; a second one set to the same number says `slot taken` and publishes
nothing, rather than both writing into one bus. That holds across tracks, sets
and processes alike. The second one does not take over by itself when the
first goes away: give it another number, or move its Bus off the number and
back once the slot is free.

**After a restart the buses come back by themselves.** Reopen the set and each
NI Listen-In claims its saved number again, with its saved name, and a
Spectrogram that was reading it finds it again within about a second. Even
after a crash, a slot held by a Live that is no longer running is reclaimed.

## The transport is shared memory, and that is the whole design decision

A process-wide table inside the shared library would have been two hundred
lines and no system calls. It would also have been dead the first time a host
put the sender and the receiver in different processes — an AU under a sandbox,
a bridged VST3, a plugin scanned out of process — and the symptom would have
been a receiver that is simply always empty, with nothing in any log.

So each bus is a shared-memory segment: a header and a ring of 131072 stereo
frames — 1 MiB, of which a reader may use 122880 (2.56 seconds at 48 kHz); the
remaining 8192 are the margin it keeps between itself and the writer, for the
reason below. On macOS and Linux the segment is a POSIX shared-memory object
named `/nia.bus.NN`, on Windows a named file mapping, `Local\nia.bus.NN`. One
writer, any number of readers, and **no coordination between the readers at
all** — each keeps its cursor in its own memory and the segment is written by
exactly one participant.

`abus_pusher_push` runs on the audio thread and allocates nothing, locks nothing
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

**A crashed host leaves the segment behind** on macOS and Linux, where shm
outlives its process, to the next reboot. A claimer reclaims a slot only when
the system says the holder's process is gone — `kill(pid, 0)`, or on Windows
`OpenProcess` and its exit code — and the reclaim is a single compare-and-swap,
so two senders racing for a dead slot cannot both win. A recycled pid can make
a dead holder look alive; that costs a bus number until the other process exits,
which is the safe way to be wrong. On Windows a name lives only as long as a
handle to it, so a crashed host takes its bus with it.
`engines/audio-bus/README.md` has the details.

## One known limit

If a host sandboxes the plugin on macOS, its shm names need an app-group prefix
and this will not connect — the editor says `unavailable` rather than pretending.
Live loads VST3 plugins in process, which is what it was built for.

## The plugin around the bus

NI Listen-In is a VST3 on the house JUCE shell (`cmake/NiJucePlugin.cmake`),
with a native editor (`editor/`), for macOS (Linux and Windows follow). It took over
from the earlier builds without breaking a set: the same bundle name and VST3
class, Bus at the same parameter ID, and the same saved state, byte for byte.
`ListenIn.h` says which thread holds which half of a claim; `li_processor`,
`li_rt` and `li_host` hold the processor, its audio callback and the built
bundle to all of it.

## Seeing it work without a receiver

`abus_tap` is built with the tests and answers "is it actually routing?"
without opening a Spectrogram:

```
abus_tap        # every slot, what is on it, and what it is called
abus_tap 3      # follow slot 3: peak, frames, frames dropped
```

It is also the worked example of the receiving ABI, in sixty lines, beside
`audio_bus.h`, which the build generates from
`engines/audio-bus/crates/bus-capi/src/lib.rs`.
