# audio-bus

A **shared-memory audio bus** between plugins in one host. One writer claims a
numbered slot and publishes stereo float audio; any number of readers, in that
process or another, open the same slot and read it.

MIT, © 2026 Torben Gräber. **No dependencies at all** — it declares the
handful of macOS calls it needs in `crates/bus-core/src/shm.rs` rather than
taking libc,
which is MIT/Apache-2.0 and would be fine, but is a large thing to borrow
`mmap` from.

```
cargo test -p bus-core -p bus-capi
```

**Tests run in a private namespace.** The shm names are global to the user, so
a test run would otherwise share — and unlink — the slots of another checkout's
tests, or of a Live session on the same machine. When `NIA_BUS_NS` is set,
every name becomes `/nia.<hash>.NN` instead of `/nia.bus.NN`. The workspace's
`.cargo/config.toml` sets it to the checkout's path for every `cargo test`
(bus-core's `tests_never_touch_the_production_names` fails if it is missing),
and the C tests set a per-process value before their first bus call. Nothing
sets it in a host, so plugins use the real names.

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

## Who can see a bus: the trust model

A bus is a POSIX shared-memory object named `/nia.bus.NN`, created with mode
`0600`. So:

- **Only processes running as the same user can open it at all** — to read or
  to claim. Another account on the machine gets `EACCES`.
- **Every process of that user is trusted equally.** The names are fixed and
  public; any program you run can read what a Listen-In publishes, publish into
  a free slot, or `shm_unlink` a slot's name. There is no authentication
  between sender and receiver, by design: they are your own plugins in your
  own host.
- A reader maps the segment **read-only**, so a receiver cannot corrupt a bus
  even by mistake, and treats everything it reads as untrusted — sizes and
  layout are checked before anything is mapped or interpreted.

That is the right boundary for audio between plugins in one session. It would
be the wrong one for anything secret: do not put anything on a bus you would
not show another program running as you.

## The shape of it

| | |
|---|---|
| `header.rs` | the segment layout, the owner word, and the label's seqlock — every field an atomic |
| `ring.rs` | the wrap, the lap detection, the resync — over a `&Header` and a `&[AtomicU32]`, so a test can build one on the heap |
| `shm.rs` | `shm_open`/`mmap`, and the two doors: only a writer may use the one that creates, and a reader's is read-only |
| `lib.rs` | `Writer` + `Pusher`, `Reader`, `probe`, and the claim protocol |
| `crates/bus-capi` | the C ABI; `include/audio_bus.h` is the contract |

## One writer, N readers, and no coordination between them

A reader keeps its cursor **in its own process memory**, never in the segment,
and maps the segment read-only. So the stream is written by exactly one
participant and two readers cannot interfere with each other, because neither
of them writes anything at all. That is what makes the whole thing lock-free
without being clever.

The samples and every header field are atomics. A reader copying while the
writer overwrites the same cells is the design — the re-check afterwards throws
such a copy away — and with plain floats that overlap would be undefined
behaviour however carefully the result was discarded. Relaxed atomic loads and
stores compile to the same moves.

The 32-byte label cannot be read in one atomic; it is a seqlock, and a reader
that keeps losing the race keeps the name it had rather than spinning on the
message thread.

## The thread rules are part of the ABI

```
Writer::claim / drop / set_label / set_sample_rate      the main thread
Pusher::push                                            the audio thread, and only it
Reader::open / reattach / drop                          the main thread
Reader::read                                            one thread, the same one each time
```

A claim returns two handles, a `Writer` for the main thread and a `Pusher` for
the audio thread, and each mutates only through `&mut self` — so safe Rust
cannot push from two threads at once. The C ABI keeps one handle and projects
each call to the half its thread owns. A sample-rate change is **posted**: the
main thread leaves a request and the next `push` applies it, because the
restart resets the frame count that only the audio thread may write.

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

So a claim may take over a held slot, but only on one piece of evidence:
`kill(pid, 0) == ESRCH`, which concludes the holder is **gone** — never that one
is alive. A recycled pid can make a dead holder look alive; that costs a bus
number until the unrelated process exits, and is the conservative way to be
wrong. (There is no heartbeat: a stalled heartbeat cannot tell a dead host from
a paused one, so it could only ever have been a second opinion nobody acted on.)

**The claim is one compare-and-swap.** Who holds the slot and how many times it
has been claimed live in one 64-bit owner word, and a claimer swaps from the
exact word it judged to its own. Two reclaimers of one dead pid both judge the
same word; whichever swaps first changes it, and the other re-judges a holder
that is alive. An earlier protocol kept state and pid in separate words, and
both reclaimers won — `two_reclaimers_of_one_dead_pid_do_not_both_win` in
`src/claims.rs` places that interleaving deterministically.

## A clean quit removes the bus, and readers follow a new one

A departing writer **unlinks the name, then frees the slot**, in that order: a
claimer that opened the segment just before the unlink finds it free only once
it has stopped being the bus, and every claim ends by checking that the slot's
name still leads to the segment it claimed (each segment carries a unique
`incarnation`). A claim on an orphan is let go and retried.

A reader still mapping the old segment sees a sender that stopped, and nothing
in a mapping can say that the name moved on. `Reader::reattach`
(`abus_reader_reattach`) asks the name again and moves to the new segment,
reporting a resync. It makes system calls, so it is a main-thread call for a
reader that has been getting nothing for a while — which is when spectro-recv
makes it.

## Versions

`ABI_VERSION` in `header.rs` names the layout **and** the claim protocol. A
reader refuses a segment of another version; a writer replaces it. Version 2
introduced the owner word and the incarnation and dropped the heartbeat, so a
version-1 plugin and a version-2 plugin cannot share a slot — the second to
claim replaces the first's segment.
