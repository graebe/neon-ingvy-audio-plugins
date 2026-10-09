---
title: Native UI
order: 7
slug: native-ui
---

The editors are native JUCE 9 Components, built to the published
Ultraviolet 1.1.0 system (`design/scheme/project`). Nothing an editor does
depends on a display refresh: every clock in it is a timer.

## Where things are

| Path | What |
|---|---|
| `plugins/_shared/ui/src/` | the kit: `ni_ui`, every `*.cpp` here, globbed |
| `plugins/_shared/ui/fonts/`, `assets/` | JetBrains Mono 2.304 (OFL), the ground's grain tile |
| `plugins/_shared/ui/gallery/` | the gallery app and its pages (`pages/*.cpp`, globbed) |
| `plugins/<product>/editor/` | one editor: `Model.h`, the editor, its parts (globbed) |
| `tests/ui/` | the test programs, the snapshot harness, the fakes, `baselines/` |
| `scripts/gen-tokens.mjs` | writes `UvTokens.h` and the grain tile from the design mirror |

## Build and test

The kit, every editor, the gallery and their tests are part of the root
build, so the presets build them (see **Build & CI — Ableton Live**):

```sh
scripts/test.sh quick                 # unit tests and the token guard, build-dev/
scripts/test.sh full --no-coverage    # and the snapshot goldens, build/
cmake --build build-dev --target ni_ui_gallery   # the gallery app
```

Every source list is a `CONFIGURE_DEPENDS` glob, so a new component, page,
editor file or test is a new file and no CMake changes.

`ni_ui` and each `ni_editor_<product>` are interface libraries that carry
their sources: a JUCE module is compiled once in each binary, with that
binary's flags, and so is the kit. The root build
`add_subdirectory(plugins/_shared/ui)`s it and calls
`ni_ui_add_editor(<product>)` for each product, whose plugin links the editor.

The tests are doctest, like every other C++ test here. `ni_ui_tests` is the
kit's (every `tests/ui/*.cpp` that is not an editor's), `ni_ui_tests_<product>`
an editor's (`tests/ui/<product>_*.cpp`, the product as its directory is
named), so a half-written editor breaks only its own program. Each program is
registered twice: `<program>_unit` (quick) and `<program>_snapshots` (full), a
golden being any test case declared with `NI_SNAPSHOT_TEST`. The token guard,
`ui_tokens_native`, is quick.

## Two namespaces

`uv::` is the design system as code: no state, no behaviour.

| Header | Gives |
|---|---|
| `UvTokens.h` | `uv::tok`: every token, generated, the only file that may spell a colour |
| `UvType.h` | `uv::fonts()`, the six styles (`uv::type::value()` ...), `draw`, `drawValueWithUnit` |
| `UvIcons.h` | `uv::icon(name, colour)`, `uv::drawIcon`, `uv::tint`, the fifteen names |
| `UvLight.h` | `uv::light::glowLed`, `glowFocus`, `glowArc`, `shadow`, `dropShadow` |
| `UvGround.h` | `uv::ground::paint`, the window ground at rest |
| `UvLookAndFeel.h` | `uv::LookAndFeel`, `uv::ReadoutLabel`, `uv::SharedLookAndFeel` |

`ni::ui::` is the kit's machinery, and where its components go.

| Header | Gives |
|---|---|
| `EditorModel.h` | the part of every editor's model that is not the product's |
| `ParamBinding.h` | a host parameter as a control sees it |
| `FrameClock.h` | the one display-rate tick of an editor |
| `Fit.h` | `FixedDesign`, `fitScale`, `constrainToDesign` |
| `Keys.h`, `Detents.h` | the keyboard map and the detents |
| `Focus.h` | `FocusVisibility`: focus that shows only for the keyboard |
| `Info.h` | the Hint's info: `setInfo`, `InfoText`, `InfoState`, `InfoTracker`, `InfoHost` |
| `Luminous.h` | light past a component's edge, painted by its parent |

## The music views

Five components and a model, for any editor that shows notes (NI
Chord-Detector's first). Each takes a plain value State and draws only from the
tokens; the words and the spelling are always the owner's, from the engine.

| Header | Gives |
|---|---|
| `Music.h` | `music::NoteSet` (128 bits), `isBlack`, Bravura and its SMuFL glyphs (`drawGlyph` at a glyph's own origin, the em four staff spaces) |
| `Keyboard.h` | whole octaves from a C, held keys lit uv (uv-deep dimmed), the bass marked under; `lowestToShow` follows the notes. Display only |
| `CircleOfFifths.h` | twelve keys clockwise in fifths: the key's seven tinted, sounding ones lit, the root filled, the tonic's amber ring; `onTonicSelected` from a click or the arrows (a fifth a step), one Tab stop; `centre()` for the owner's mode select. `Luminous` |
| `ChordReadout.h` | the readout (28/32), the roman numeral chip, the HELD LED, the words in capitals, NOTES and ALSO. Display only |
| `NoteHistory.h` | the model: notes with start and end on a clock that only goes forward, chord names at each change, the clock's tempo and bar lines; bounded |
| `HistoryView.h` | what both views share: now at the right edge, `span` quarters across, the host's bar lines |
| `GrandStaff.h` | proportional notation on a grand staff with the key signature: a head at each onset, a tail for its length, an accidental where the key differs; seconds side by side. Where a note sits is the owner's `Writer` (the engine's `cd_write_note`) |
| `PianoRoll.h` | one numbered line per note over the played range (at least 24), a bar per note |

Bravura is embedded as JetBrains Mono is (`fonts/bravura/README.md`), and a
bundle that draws notation carries its `OFL.txt` (`NI_UI_MUSIC_FONT_LICENSE`).
The gallery's Music section shows each in its states; `tests/ui/music.cpp`
holds their behaviour and pictures.

## The plots

`Plot.h` is the PlotWell card: `ni::ui::PlotWell`, the well an editor's plot
derives from and draws in (`paintPlot`), and in `ni::ui::plot` the marks the
card allows inside it (`curve`, `under`, `ghost`, `dry`, `wet`, `rule`), the
millisecond `Axis`, and `band`, the min/max envelope of an engine's capture.
A plot draws what its engine rendered or captured, never a second model of the
DSP.

**An audio trace follows its material.** A well that draws one calls
`enableLevelRange()` and feeds `followLevel(peak, nowMs)` from its frame tick,
with the capture's peak (`plot::peak`); it builds its bands on
`levelRange().fullScale()` (`BandGeometry::fullScale`) and again whenever
`followLevel` says the range moved or `levelChanged()` is called.
`plot::LevelRange` is the rule: the loudest level shown over the last two
seconds, up to the next 6 dB rung between 0 and −48 dBFS, wider in the frame a
louder peak arrives, narrower only after the window and 1.5 dB under the rung,
gliding there with a 300 ms time constant. The well writes the range ("−12 dB")
at the far end of its caption band, and a double-click — or an accessible press
— holds it at full scale ("0 dB fixed") for the life of the editor. Only the
audio zooms: a gain, gate or envelope over it stays on its own scale. The Side-Chain's
well and the Trance Gate's Signal plot use it; `tests/ui/plot.cpp` holds the
rule on a clock the test turns.

## The design layer

**Tokens are generated.** `node scripts/gen-tokens.mjs` reads `tokens.json`
and the motion constants `ground.js` publishes, and writes `UvTokens.h`
(`uv::tok::colour::ink`, `uv::tok::space::space4`, `uv::tok::size::controlH`,
`uv::tok::type::label`, `uv::tok::shadow::glowLed`,
`uv::tok::motion::ground::fps` ...). The names are the system's, camel-cased.
`tests/ui_tokens_native.test.mjs` fails while the file is stale, and while any
kit or editor source spells a colour: a hex word, a `Colours::` name, a colour
of literals, a `#rrggbb` in a string. Derive instead (`.withAlpha()`,
`.interpolatedWith()`); a colour the system lacks goes to the design first.

**Every glyph is an embedded face.** The kit's fonts carry JetBrains Mono's
own faces, never a name for the system to resolve, and `uv::LookAndFeel`
answers any font JUCE makes for itself with them. Sizes are CSS px, so a
style's size is the em square (`withPointHeight`), and tracking is CSS
letter-spacing. Title and label are capitals (`uv::type::cased`).

**Icons are currentColor.** `uv::icon("copy", colour)` is the design mirror's
own SVG in the colour of its control: ink, on-uv on a lit button, ink-dim
disabled, bg-000 on red.

**Light is drawn as the design system draws it.** glow-led and the arc glow
are CSS `drop-shadow()` filters there, whose length is the standard deviation;
glow-focus is a box-shadow, whose blur is a radius. `UvLight.h` reproduces
both, the chain of glow-led's two filters included. A component cannot paint
past its bounds, so a control whose light reaches past them is `Luminous`:
it paints only its light in `paintLight()`, and every container ends its
`paint()` with `ni::ui::paintChildLights(g, *this)`, over its background and
under its children.

## The model

**An editor talks to its plugin through its model, and nothing else.**
`plugins/<product>/editor/Model.h` derives from `ni::ui::EditorModel`:

- **Parameters**: `parameter(index)` is a `juce::RangedAudioParameter&`, in
  VST3 ID order, which is the earlier builds' index
  (`tests/fixtures/iplug2/<product>/parameters.json` lists them). A control
  binds one with `ni::ui::ParamBinding`, over `juce::ParameterAttachment`:
  a drag is `begin()` on its first move, `input()` per move and `end()`; a
  click or a key is `commit()`, one complete gesture that writes nothing when
  nothing changes; `reset()` goes to the plugin's default; host and
  automation changes arrive on the message thread through its callback.
- **Display text** is the parameter's: `getText` and `getValueForText`, which
  the processor takes from the Rust engine. The editor holds no units,
  precisions or enum labels. Tests supply the text through
  `ni::ui::test::FakeParameters` (`tests/ui/fakes.h`), which also records
  every gesture the host would see.
- **Product data** is plain C++: snapshot getters (the pattern, the playhead,
  a scope, the buses) and commands (toggle a step, copy a slot, name a bus).
  No message tags, no strings where the data is a number. Every call is on
  the message thread; a getter returns the latest snapshot already copied
  (the processor reads the engine's triple-buffered snapshots), a command
  returns at once and shows in a later snapshot.
- **The Ground's rings** come from `takeRings()`, counted by the plugin from
  the host's transport; **Motion** is remembered per machine, never in the
  host's state.

Tests implement the model with fakes; the processor implements it later.

## Rules every editor keeps

**State lives in the model and the controls; timers only repaint.** One
`ni::ui::FrameClock` per editor ticks on `juce::VBlankAttachment`, with a
timer standing in when no vblank comes, and only while something
subscribes. A missed tick leaves a picture late, never wrong.

**A fixed design size, scaled to fit.** The editor lays out at its design
size, always; `ni::ui::FixedDesign` scales it from the top-left corner to
whatever the host gives, and `constrainToDesign` keeps the window in
proportion. Nothing reflows and nothing scrolls.

**Every pointer control takes the keyboard.** Arrows step (Shift fine),
Page Up/Down take large steps or jump between detents, Home/End go to the
ends, Space or Enter press, Enter types into a readout; a step grid is one
Tab stop (`Keys.h`). Every keystroke is its own gesture. A control's focus ring
(`glowFocus`) and its info show only for keyboard focus (`FocusVisibility`).

**Every control says what it does, in at most 72 characters.** One line,
*Name — what it does*, set with `ni::ui::setInfo`; it is also the control's
accessible description. Each editor keeps its lines together in one place,
written as `ni::ui::InfoText` constants, which fail to compile past the limit.
The editor's root is an `ni::ui::InfoHost` with an `InfoTracker`: the bar shows
an outcome first, then the line under the pointer, then the one on the
visible keyboard focus, then the window's conventions, and a line leaves
after a 150 ms grace. Each editor's test runs `NI_CHECK_INFO_LIMIT` on each of
its states.

## Snapshots and the gallery

`tests/ui/snapshot.h`: `NI_CHECK_SNAPSHOT(component, "name")` renders with
JUCE's software renderer and compares with
`tests/ui/baselines/<name>-<os>.png` (darwin, linux, windows: glyph outlines
differ by platform). A pixel differs when a channel is more than 4 of 255
away; a picture fails when more than 0.01 % of its pixels do. A missing
baseline fails. On a failure `<name>-<os>.actual.png` and `.diff.png` are
written to `<build>/tests/ui/snapshots/`. `NI_UPDATE_BASELINES=1` writes the
baselines of the snapshots that run instead; narrow it with doctest's
`--test-case`, and look at every picture before committing it. An editor's
baselines are named after it: `<product>-<state>`.

The gallery (`NI UI Gallery`) shows every registered page. A page is a file
in `plugins/_shared/ui/gallery/pages/` that registers itself with
`NI_GALLERY_PAGE("Controls", "Knob", factory)` and shows a component in each of
its states. `"NI UI Gallery" --shots <dir>` writes every page as a PNG at 2x,
for looking at without a screen.
