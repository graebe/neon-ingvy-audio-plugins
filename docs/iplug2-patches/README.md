# iPlug2 patches (proposed, not applied)

Fixes for bugs in iPlug2 that clap-validator 0.4.1 and host-conformance work
found in our pinned iPlug2 (`d54f690`). They are `git format-patch` output
against that commit. The submodule itself is untouched.

| patch | fixes |
|---|---|
| 0001-validator-fixes | CLAP `stateSave` on a short write, `stateLoad` without a params rescan, `audioPortsGetConfig` main-port flags and channel counts, `IParam::GetDisplay` not rounding stepped values |
| 0002-clap-bus-offsets | CLAP packs input buses back to back instead of at per-bus offsets as VST3/AU do (Side-Chain key position) |
| 0003-getstr-au-restore | `IByteGetter::GetStr` accepting lengths past the end; AU `SetState` treating -1 as success |
| 0004-soft-bypass-opt-in | lets a plugin handle host bypass itself so a gate can crossfade instead of hard-switching |

With 0001 applied, NITranceGate, NISideChain and NIListenIn pass clap-validator
with no failures. NISpectrogram's remaining failure is a clap-validator bug:
`param-conversions` divides by the parameter count, and this plugin has none.

How to carry them is an open decision: a pinned fork of iPlug2 as the
submodule, upstream pull requests, or both.
