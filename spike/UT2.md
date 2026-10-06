<!--
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) 2026 Torben Gräber
-->
# UT2: does Live open an iPlug2 Trance Gate set with the JUCE build?

This is the question the refactor cannot answer without Live. The spike is a
minimal JUCE 9 NI Trance Gate: the same engine, parameters and saved-state
format, with JUCE's generic editor (sliders, no pattern grid). Its tests prove
that it reads every state the iPlug2 build saves, and that it sounds the same
to the sample (`ctest --test-dir build-spike/juce-trance-gate`). What they
cannot prove is that **Live offers an old set to a plugin with a different
VST3 class ID**. That is what this test checks.

There are two builds, and Live decides between them:

| | Bundle | How an old set finds it |
|---|---|---|
| **A** | `spike/out/NI Trance Gate.vst3` | JUCE's own class ID, with the iPlug2 class declared compatible in `moduleinfo.json` and through `IPluginCompatibility`. This works **only if Live reads that**. Live's release notes up to 12.4.6 do not mention it, and Ableton's forum said in 2024 that Live does not support it, so **A is expected to fail**. |
| **B** | `spike/out/same-class/NI Trance Gate.vst3` | **The iPlug2 class ID itself** (`JUCE_VST3_COMPONENT_CLASS`). A set finds B exactly as it found the old build, so no support from Live is needed. |

Run A first, then B, each on a fresh copy of the set.

## Before you start

1. **Make a reference set** with the installed iPlug2 NI Trance Gate
   (v2026.10.06.5). Use one track and a steady sound (a sustained pad or noise),
   and put NI Trance Gate on it. Give **slots 1 to 3 different patterns and
   sounds** (Rate, Length, Amount, Width, the envelope, Fade). **Leave slot 2
   selected.** Automate one parameter, for example Amount. Write down what you
   see per slot, or take screenshots.
2. **Record a reference.** Resample 4 bars of the gated track to an audio track
   from bar 1. This is used for the null test.
3. Save the set as `UT2 iPlug2.als` and **quit Live**.
4. **Copy the whole project folder twice**, to `UT2 test A` and `UT2 test B`.
   A set that is re-saved while build A is loaded points at A's class from then
   on, and the iPlug2 build cannot open it again. Keep the original untouched.

## Move the iPlug2 VST3 aside, Live closed

Live must not find the old class, or it will simply use it:

```sh
mkdir -p ~/Music/NI-UT2-aside
mv ~/Library/Audio/Plug-Ins/VST3/NITranceGate.vst3 ~/Music/NI-UT2-aside/
mv ~/Library/Audio/Plug-Ins/Components/NITranceGate.component ~/Music/NI-UT2-aside/
```

The set asks for the VST3, so that bundle is the one that matters. The AU
goes too: if Live ever fell back to the iPlug2 AU, "it loaded" would prove
nothing. The other three NI plugins are not involved.

## Test A: the compatible class

```sh
cp -R "<worktree>/spike/out/NI Trance Gate.vst3" ~/Library/Audio/Plug-Ins/VST3/
```

`<worktree>` is `/Users/torben/Music/code/vst/worktree/feature/juce-spike`.

1. Start Live. Go to **Settings → Plug-Ins**, make sure **Use VST3 Plug-in
   System Folders** is on, and **Option-click Rescan** (a full rescan). In the
   browser, under Plug-Ins → VST3 → Neon Ingvy, there is one **NI Trance Gate**.
2. Open `UT2 test A/UT2 iPlug2.als`.

**Pass**: the device loads as NI Trance Gate, not as a missing plug-in. Then
check:

- Slot reads 2, and every other parameter reads what slot 2 had: open the
  device's parameter list, or the generic editor (wrench icon).
- Switching Slot to 1 and to 3 moves the other parameters to those slots'
  sounds (Rate 1/8 on slot 1, for example), and the rhythm changes to that
  slot's pattern.
- The automation lane is still attached to Amount and moves it during
  playback.
- **The null test.** Play bars 1 to 4 with the reference track's Utility set
  to invert phase, both tracks at unity. The sum must be silent, or very
  nearly. The spike's tests find the two builds' outputs identical to the
  sample.

**Fail**: Live reports NI Trance Gate as missing, or the device loads at its
defaults (Slot 1, 16 steps, 1/16, everything else untouched). Write down
Live's exact message.

## Test B: the same class (whatever A did)

Quit Live first, then:

```sh
rm -rf ~/Library/Audio/Plug-Ins/VST3/"NI Trance Gate.vst3"
cp -R "<worktree>/spike/out/same-class/NI Trance Gate.vst3" ~/Library/Audio/Plug-Ins/VST3/
```

Option-click Rescan, open `UT2 test B/UT2 iPlug2.als`, and make the same
checks. **B and the iPlug2 VST3 must never be installed together**, because
they claim one class ID.

One known difference in B: the plugin's own Bypass parameter has VST3 ID 15
instead of iPlug2's 65536. That only matters if a set automates that
parameter, not Live's device on/off. Its saved on/off state is still
restored.

## What to report

For A and for B: loaded or missing, Slot and the values per slot, whether
automation follows, and the null test's result. A screenshot of Live's message
if it shows one.

## Rollback

Quit Live, then:

```sh
rm -rf ~/Library/Audio/Plug-Ins/VST3/"NI Trance Gate.vst3"
mv ~/Music/NI-UT2-aside/NITranceGate.vst3 ~/Library/Audio/Plug-Ins/VST3/
mv ~/Music/NI-UT2-aside/NITranceGate.component ~/Library/Audio/Plug-Ins/Components/
```

Option-click Rescan. The original `UT2 iPlug2.als` and every other set open
with the iPlug2 build as before. A test copy that was re-saved during **B**
opens there too, because it still names the iPlug2 class, and the iPlug2 build
reads the spike's state (`spike_host` checks this). A copy re-saved during
**A** names A's class, so throw it away.

## Rebuilding the bundles

From the worktree. The first two commands build the iPlug2 reference that
the tests compare against; they install nothing.

```sh
cmake -S . -B build-spike -DCMAKE_BUILD_TYPE=Release -DIPLUG_DEPLOY_PLUGINS=OFF
cmake --build build-spike -j
cmake -S spike/juce-trance-gate -B build-spike/juce-trance-gate -DCMAKE_BUILD_TYPE=Release
cmake --build build-spike/juce-trance-gate -j --target spike_out
cmake -S spike/juce-trance-gate -B build-spike/juce-trance-gate-same-class \
      -DCMAKE_BUILD_TYPE=Release -DNI_SPIKE_SAME_CLASS=ON
cmake --build build-spike/juce-trance-gate-same-class -j --target spike_out
scripts/validate-plugins.sh fetch build-spike/validators   # pluginval v1.0.4
ctest --test-dir build-spike/juce-trance-gate --output-on-failure
ctest --test-dir build-spike/juce-trance-gate-same-class --output-on-failure
```
