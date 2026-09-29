---
section: live
title: Ableton Live Interface
---

The whole interface is the picture. There are no parameters to automate and
nothing to set up: insert it, and it draws what is passing through.

## Reading it

Frequency is the vertical, logarithmic, 10 Hz at the bottom and 20 kHz at the
top across 256 bands — about two per semitone, so a semitone is roughly two
rows. Time scrolls right to left and the visible window holds thirteen seconds.

Brightness is level: the ground is quiet, a near-white core is loud, and the
violet between them is the falloff.

## Pause holds the view, not the analysis

The columns keep arriving and keep filling the history behind the frozen
picture, so letting go shows a view that is already current — with the paused
seconds *in* it, scrolled past, rather than cut out of it.

The plugin is never told that you paused; it is a repaint gate in the editor.
Which is why pausing cannot affect the audio, and why the history behind the
freeze is real rather than reconstructed.

## The range dropdown zooms

| range | span |
|---|---|
| Full | 10 Hz – 20 kHz |
| Sub | 10 – 200 Hz |
| Bass | 40 – 800 Hz |
| Mid | 200 Hz – 4 kHz |
| High | 2 – 20 kHz |

They overlap on purpose. All 256 bands spread over whatever is chosen.

It does **not** add bins — the window is what decides those. What it does is
give the ones that exist the whole height: in Sub, ~33 bins crowded into the
bottom 40 pixels become ~33 bins at 8 pixels each, which is what makes 50 Hz and
60 Hz two visibly different rows. Higher up there are several bins to a band
already, and the zoom is detail in the ordinary sense.

Changing it while audio runs takes no lock and allocates nothing:
`spectro_set_range` stores a request, the audio thread rebuilds its own band
table at the next frame, and columns measured against the old range are dropped
rather than drawn under the new scale.

## Installing

The release carries an unsigned universal bundle in all three formats. macOS
will refuse to load it until the quarantine attribute is removed:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Spectrogram.vst3
```

## For developers

The column format on the wire, and the mount-ordering bug that tag `102` exists
to fix, are in [plugins/spectrogram/ui/README.md](../ui/README.md).
