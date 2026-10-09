<!--
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) 2026 Torben Gräber
-->
# What the iPlug2 VST3 builds save, byte by byte

This is the contract a replacement build must read. It covers the four VST3
plugins as built by iPlug2 from v2026.09.30.1 to v2026.10.06.5. It also covers
the earlier layouts, because Live keeps whatever a set was saved with.

Everything here was read in the source (the references are given) and then
checked against the bytes. `tests/vst3_capture.mm` decodes every fixture with
exactly this layout and refuses any byte that the layout does not explain.

## What Live keeps for one instance

| What | iPlug2's answer | Where it comes from |
|---|---|---|
| Class ID | FUID `F2AEE70D 00DE4F4E <PLUG_MFR_ID> <PLUG_UNIQUE_ID>`; see [ids.json](ids.json) | `IPlug_include_in_plug_src.h:101-103` |
| Component model | **Single component.** The factory has one class (`Audio Module Class`, `kSimpleModeSupported`). The `IComponent` is also the `IEditController`. `getControllerClassId` returns `kNotImplemented`. | `IPlug_include_in_plug_src.h:139-148`, `vstsinglecomponenteffect.h` |
| Component state | The stream below | `IPlugVST3_Common.h:25-46` |
| Controller state | **Always empty (0 bytes).** The SDK's `SingleComponentEffect` renames `IEditController::getState`/`setState` to `getEditorState`/`setEditorState` (`vstsinglecomponenteffect.h:23-24`). iPlug2 implements both as no-ops that return `kResultOk` (`IPlugVST3.cpp:154-164`). | |
| Parameter IDs (automation, Live's parameter list) | Each plugin parameter's **index** `0..N-1`; **Bypass = 65536**; MIDI CC parameters from **65538** (Side-Chain only) | `IPlugVST3_ControllerBase.h:81,114`, `IPlugConstants.h:59-61` |

The class ID's sixteen bytes depend on the platform. On macOS (`COM_COMPATIBLE`
is 0), the bytes are the four words big-endian, which is also the 32-digit
string that hosts and `moduleinfo.json` write. On Windows, the first eight
bytes are in GUID order (`funknown.h:36-47`). The string stays the same, but
the bytes do not. `ids.json` gives both. The JUCE builds name the old class by
its bytes (`VST3Interface::hexStringToId`), so a Windows build must use the
Windows bytes.

## The component state stream

All integers are little-endian `int32`. iPlug2 copies host-endian memory
(`IByteChunk::Put`), and every platform it ships on is little-endian. Every
double is an IEEE-754 binary64, also little-endian.

```
offset     size   field
0          8      magic          4E 49 73 74 00 00 F8 7F   "NIst" + a quiet-NaN tail
8          4      int32 version  the product's chunk version: 1 for all four so far
12         4      int32 B        bytes of body that follow the header
16         8*N    double[N]      the N plugin parameters, by index, PLAIN values
16+8N      ...    strings        int32 length L (>= 0, no terminator) + L bytes (UTF-8),
                                 product by product (below), up to the end of the body
16+B       4      int32 bypass   0 or 1, appended after the chunk by the VST3 wrapper
                                 total = 20 + B bytes
```

- **Header**: written by the iPlug2 builds' `shell_state.h` (removed with
  them) and read by the JUCE shell's `plugins/_shared/juce/Nist.h`. The magic decodes to a
  NaN when it is read as a double. No parameter can hold a NaN, so a chunk
  with the header can never be mistaken for one without it.
- **Parameters**: `IPluginBase::SerializeParams` (`IPlugPluginBase.cpp:109-122`)
  writes `IParam::Value()` for each parameter. That is the plain value in the
  parameter's own units (Slot `2`, Amount `80`), **not normalized**.
- **Strings**: `IByteChunk::PutStr` (`IPlugStructs.h:189-194`).
- **Bypass**: `IPlugVST3State::GetState` (`IPlugVST3_Common.h:43-44`) writes
  `GetBypassed()` after the chunk. `SetState` (`:50-85`) reads the whole
  stream and parses the chunk from 0. It then seeks to where the chunk parse
  stopped (`16 + B`) and reads the bypass. **If those 4 bytes are not there,
  the whole setState fails.** Any state that an iPlug2 build has to read must
  end its chunk with them.
- **The controller side of a load**: `SetState` puts the saved bypass into the
  controller's Bypass parameter (`UpdateParams`, `IPlugVST3_ControllerBase.h:336`)
  and **not into the processor**. The processor runs unbypassed until a host
  sends it the parameter, and a save made straight after the load writes 0.
  The fixtures record this as `reload.bypassSavedAfterReload`. A replacement
  build should restore the flag that the stream carries.

### How iPlug2 decides a chunk is valid

A replacement build should apply the same checks. A chunk that fails them
leaves the instance as it was, and the host's setState call fails.

1. A stream with the magic must have `B >= 0` and `16 + B <= stream size`.
   Otherwise it is refused (`shell_state.h` `Read`).
2. A stream without the magic is a **legacy** chunk, and its body starts at 0
   (see the last section). An empty stream is refused.
3. Every parameter double must be finite. A stepped parameter (int, enum,
   bool: the ones with `stepCount > 0` in `parameters.json`) must be a whole
   number with a magnitude of at most 2^31-1. **Ranges are not checked.** An
   out-of-range value is clamped when it is applied, because a range may
   narrow between versions (`shell_state.h` `CheckParams`).
4. Every string's length must be >= 0 and must fit inside the body
   (`shell_state.h` `GetStr`).
5. Everything is read before anything is applied. Bytes after the last field
   that the build knows, up to `16 + B`, belong to a later version and are
   skipped.

## Per product

### NI Trance Gate: version 1, N = 15, one string

| ID | Parameter | Plain value | Default | stepCount | To the engine (`Params.cpp` `ToEngine`) |
|---|---|---|---|---|---|
| 0 | Slot | int 1..8 | 1 | 7 | `- 1` (slot index) |
| 1 | Length | int 1..128, units "steps" | 16 | 127 | `- 1` |
| 2 | Rate | enum 0..12, labels from `tg_core_rate_label` | 7 (1/16) | 12 | same |
| 3 | Join Neighbors | bool Off/On | 0 | 1 | same |
| 4 | Env Time | enum ms / % | 0 | 1 | same |
| 5 | Env Curve | enum Linear / Exponential / S-Curve | 0 | 2 | same |
| 6 | Amount | double 0..100 % | 100 | 0 | `/ 100` |
| 7 | Width | double 5..100 % | 100 | 0 | `/ 100` (the engine's `hold`) |
| 8 | Attack | double 0..200 % of the gate | 1.6 | 0 | same |
| 9 | Decay | double 0..200 % | 16 | 0 | same |
| 10 | Sustain | double 0..100 % | 100 | 0 | `/ 100` |
| 11 | Release | double 0..200 % | 16 | 0 | same |
| 12 | Fade | double 0..100 % | 100 | 0 | `/ 100` |
| 13 | Fade Shape | bool Hard/Soft | 0 | 1 | same |
| 14 | Fade Dir | enum In / Out | 0 | 1 | same |
| 65536 | Bypass | off / on | off | 1 | (the trailing int32) |

The string is **the engine's state blob**: the text `tg_shell_save` writes
(tg-core `state.rs`, format `sv` 7). It is a flat JSON object. The top-level
keys are the current slot's sound and the current slot (`"slot"`, 0-based).
`p0`..`p7` are each slot's pattern, written as
`"<steps hex>:<ties hex>:<length>:<depths>[:<orders>]"`. `s<N>` is written only
for a slot whose sound differs from the top level. The blob may be **empty**:
builds before 3152b65 could save without one. An empty blob restores no
pattern, and the parameters still apply.

**How a load applies it** (`Patch.cpp` `Load`, `tg_shell_load`):

1. The blob is loaded first. It restores all eight slots' patterns and sounds.
2. The 15 parameters are then written, converted with `ToEngine`, into the
   current slot. They override the blob's rounded copy of that slot's sound.
   A set therefore reopens with its parameters bit for bit.
3. A blob with `sv` below 7 has one sound for all eight slots. The restored
   parameters are that sound, and they are spread to every slot.

The parameters are **the current slot's**. Slot is the only one that does not
belong to a slot. When Slot moves, the engine's values for the new slot take
over, and the host's parameters follow (`tg_shell_take_params`).

### NI Side-Chain: version 1, N = 15, one optional string

Every setting is a parameter (`PLUG_DOES_STATE_CHUNKS 0`). The state still
passes through `SideChain::SerializeState`, which puts the header in front.

Since v2026.10.10.2 the body may end in **one optional string**: the kick the
plot draws behind the duck (`plugins/side-chain/Kick.h`), `key` for the
sidechain key or `bus:<n>` for Listen-In bus n (1..16). It is written only
when a kick is chosen, so a set without one is the same bytes every earlier
build wrote, and anything else in it reads as no kick.

| ID | Parameter | Plain value | Default | stepCount |
|---|---|---|---|---|
| 0 | Source | enum Cycle / MIDI / Sidechain | 0 | 2 |
| 1 | Rate | enum 0..11, sc-core's labels | 4 (1/4) | 11 |
| 2 | Time | enum ms / % of cycle | 0 | 1 |
| 3 | Delay | double -100..100 % | 0 | 0 |
| 4 | Attack | double 0..200 % | 2 | 0 |
| 5 | Hold | double 0..200 % | 8 | 0 |
| 6 | Release | double 0..200 % | 35 | 0 |
| 7 | Depth | double 0..100 % | 100 | 0 |
| 8 | Curve | enum Linear / Exponential / S-Curve | 1 | 2 |
| 9 | Channel | enum Omni, 1..16 | 1 | 16 |
| 10 | Trigger | enum note 0..127 (C1 = 36) | 36 | 127 |
| 11 | Mode | enum Trigger / Gate | 0 | 1 |
| 12 | Vel | double 0..100 % | 0 | 0 |
| 13 | Threshold | double -60..0 dB | -24 | 0 |
| 14 | Lockout | double 0..200 ms | 20 | 0 |
| 65536 | Bypass | off / on | off | 1 |
| 65538..65667 | 128 MIDI CCs, Channel Aftertouch, Pitch Bend | not automatable, unit 3 | | |

iPlug2 exports the 130 MIDI parameters for any MIDI-in VST3, with
`VST3_NUM_CC_CHANS` 1 (`IPlugVST3_ControllerBase.h:118-153`). JUCE gives its
own MIDI-CC parameters different IDs (`'mdm*'`, 0x6d636d00 and up). Any
automation on them would not carry over, but nothing in the plugin reads them.

### NI Spectrogram: version 1, N = 0, five strings

The Spectrogram has no parameters, only Bypass. Its strings are the session
(`plugins/spectrogram/State.cpp`):

| # | String | Example | Default |
|---|---|---|---|
| 0 | the buses listened to, `<slot>,<slot>,...` (digits and commas only; **required**) | `2,5` | empty |
| 1 | clash floor and balance, `<dB>:<dB>`, two decimals | `-60.00:12.00` | `-60.00:12.00` |
| 2 | the view: channels added into the picture, `<ch>,<ch>,...` | `0,1,2` | `0` |
| 3 | the comparison, `<a>:<b>:<on>` | `1:2:1` | `0:1:0` |
| 4 | the zoom in Hz, `<lo>:<hi>`, two decimals | `40.00:800.00` | `10.00:20000.00` |

Strings 1 to 4 were added one at a time. A chunk may end after any of them,
and a missing string keeps its default.

### NI Listen-In: version 1, N = 1, one string

| ID | Parameter | Plain value | Default | stepCount |
|---|---|---|---|---|
| 0 | Bus | int 1..16 | 1 | 15 |
| 65536 | Bypass | off / on | off | 1 |

The string is the bus's name in UTF-8 and is **required** (it may be empty).
On load it is cleaned (`wire::parse_label`): control characters are dropped,
and the name is cut to at most 31 bytes at a UTF-8 boundary.

## Legacy chunks (no header)

Builds before e840d7b (2026-09-30) wrote the body alone, starting at offset 0:
the parameters, then the strings, then the VST3 wrapper's bypass. iPlug2 reads
a stream without the magic in exactly this way (`shell_state.h` `Read`). The
parameter count is not stored, so it is whatever the product had when the set
was saved:

| Product | Legacy layout | Parameter count over time |
|---|---|---|
| NI Trance Gate | `double[N]`, blob string, bypass | **12** up to 2026-09-29 13:15 (trance-gate v1.0.1, tag at 2c1bb05), **14** until 16:06 that day (Fade, Fade Shape added), **15** since 056433f (Fade Dir) |
| NI Side-Chain | `double[15]`, bypass | always 15 |
| NI Spectrogram | the strings, bypass | 0. Its very first build wrote **zero bytes**, which is refused because it holds nothing. |
| NI Listen-In | `double[1]`, name string, bypass | always 1 |

**The current iPlug2 build reads every legacy chunk with today's count.** A
Trance Gate chunk saved with 12 or 14 parameters is therefore **refused**: the
blob's length and its first bytes land in Fade Shape and Fade Dir as numbers
that no build writes. Those sets reopen with the plugin at its defaults even
in v2026.10.06.5. A replacement build can do better. The count is the one `N`
in {15, 14, 12} for which `8N + 4 + L`, plus the trailing bypass, is exactly
the stream size, where `L` is the int32 at `8N`. Fade, Fade Shape and Fade Dir
then take their defaults (100, Hard, In), which is what those builds did.

## Before iPlug2: the JUCE build (2026-09-21 to 09-24)

The Trance Gate was briefly a JUCE plugin, with JUCE's default class ID
`ABCDEF01 9182FAEB 47726265 54724774` (`jucePluginId('Grbe', 'TrGt')`). That is
**also the default class ID of any new JUCE build with the same codes.** A set
from that week therefore binds straight to the new build and hands it that
build's state: the engine's blob as bare UTF-8 text starting with `{`, with no
parameters, plus JUCE's private trailer, which the JUCE wrapper strips first.
Its parameters had hashed IDs (`JUCE_FORCE_USE_LEGACY_PARAM_IDS` was off).
