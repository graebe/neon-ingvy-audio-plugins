# audio-bus

A **shared-memory audio bus** between plugins in one host. One writer claims a
numbered slot and publishes stereo float audio; any number of readers, in that
process or another, open the same slot and read it.

MIT, © 2026 Torben Gräber. **No dependencies at all** — it declares the six
POSIX calls it needs in `crates/bus-core/src/shm.rs` rather than taking libc,
which is MIT/Apache-2.0 and would be fine, but is a large thing to borrow
`mmap` from.

```
cargo test -p bus-core -p bus-capi
```

## Why it is not a product engine

`docs/tech/structure.md` says a crate belongs to exactly one product and the
engines never depend on each other. That rule is about **product** engines and
it still holds. This one is the house transport — the Rust counterpart of
`ui-kit` — and it exists so that two products can share one thing: NI Listen-In
publishes, a Spectrogram will read. It depends on no product in return, which is
the direction that matters.

## Why shared memory and not a static table

A process-wide `static BUSES[16]` inside the shared library would be two hundred
lines and no system calls, and it would satisfy every test in this crate except
one. It would also be dead the first time a host put the sender and the receiver
in different processes — an AU under a sandbox, a bridged VST3, a plugin scanned
out of process — and the symptom would be a receiver that is simply always
empty, with nothing in any log to say why.

`tests/abus_ipc.c` is the test that would fail over a process-local ring, and it
is the reason the rest of this exists. It forks, writes in the child and reads
in the parent.

The cost is a real one and is stated rather than hidden: a **sandboxed** host
needs an app-group prefix on the shm name and this will not connect there. Live
loads VST3 and AU in process.

## The shape of it

| | |
|---|---|
| `header.rs` | the segment layout both Rust and C agree on, and the label's seqlock |
| `ring.rs` | the wrap, the lap detection, the resync — over a `&Header` and a `*mut f32`, so a test can build one on the heap |
| `shm.rs` | `shm_open`/`mmap`, and the two doors: only a writer may use the one that creates |
| `lib.rs` | `Writer`, `Reader`, `probe`, and the claim protocol |
| `crates/bus-capi` | the C ABI; `include/audio_bus.h` is the contract |

## One writer, N readers, and no coordination between them

A reader keeps its cursor **in its own process memory**, never in the segment.
So the segment is written by exactly one participant and two readers cannot
interfere with each other, because neither of them writes anything at all. That
is what makes the whole thing lock-free without being clever.

The one apparent exception is the 32-byte label, which cannot be read
atomically; it is a seqlock, and a reader that keeps losing the race keeps the
name it had rather than spinning on the message thread.

## The thread rules are part of the ABI

```
Writer::claim / release / set_label / set_sample_rate   the main thread
Writer::push                                            the audio thread, and only it
Reader::open / close                                    the main thread
Reader::read                                            one thread, the same one each time
```

`push` allocates nothing, takes no lock and makes no system call —
`tests/no_alloc.rs` fails the build if that stops being true. `claim` does all
three, which is why it is not allowed anywhere near the audio thread.

## Falling behind is reported, never papered over

A reader that stalls comes back to find the writer has lapped it. The dangerous
answer is the plausible one: hand back the buffer anyway, half of it old samples
and half new, spliced at an arbitrary point. **It looks exactly like audio.** It
passes every value check. The only thing wrong with it is that those samples
were never adjacent.

`a_reader_is_never_handed_a_splice` runs a writer flat out against a
deliberately slow reader and checks every delivered block for internal
continuity. It is the only test here that needs threads, and it earned its
keep: the first version of this ring failed it about one run in three.

**The fix was not more checking.** `read` re-reads the writer's position after
copying and discards anything that was overtaken, and that was already there —
it simply cannot see the failure. `push` copies its samples and *then* stores
the new count, so for the width of one block the samples are in memory and the
counter still reads the old value. A reader in that region sees new audio while
every arithmetic check it can make says the writer has not arrived yet. The
second attempt, a fence before the re-check, changed nothing, because ordering
was never the problem.

So the reader stays out of that window instead. The writer publishes in runs of
at most `MAX_BLOCK_FRAMES` (8192), which bounds it; the reader treats the usable
history as `RING_FRAMES - MAX_BLOCK_FRAMES` and never reads inside the
remainder. The re-check stays as the second line of defence, for a reader that
was simply too slow. The margin costs 6% of the ring and is the part that makes
it correct.

## A crashed host leaves its segment behind

shm outlives the process that made it, all the way to a reboot. So a slot marked
CLAIMED is not necessarily a taken one, and refusing it forever would mean one
crash costs a bus number until you restart the machine.

The evidence is deliberately two-sided, because each half alone lies: a pid can
be recycled onto an unrelated process, and a heartbeat can be stalled by a host
that is merely paused. Only `kill(pid, 0) == ESRCH` concludes anything, and it
concludes the writer is **gone** — never that one is alive.
