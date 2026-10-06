# Bravura, as the native editors embed it

The music font of the kit's notation: the clefs, accidentals and noteheads
`ni::ui::GrandStaff` draws. Bravura is the reference font of SMuFL (Standard
Music Font Layout), so every glyph sits at its standard code point
(`U+E050` the G clef, `U+E262` the sharp, `U+E0A4` a black notehead) and is
drawn to the staff-space metrics notation engravers use.

| File | SHA-256 |
|---|---|
| `Bravura.otf` | `cdf0f893ee1fdb64b7f6713d71ee0dcfc349c0ac01429a8e451b01a9e79f5f3b` |
| `OFL.txt` | `c24929be7028026a65ee8894da1e3c36d2a4ccce0548d9ba8ba64509f46319ee` |

**Version 1.482**, the official release `bravura-1.482` of
<https://github.com/steinbergmedia/bravura/releases/tag/bravura-1.482>
(24 August 2026), `Bravura.otf` and `OFL.txt`, unmodified.

Embedded whole, as BinaryData of `ni_ui_assets`, never asked of the system.

**Licence:** SIL Open Font License 1.1, © 2015 Steinberg Media Technologies
GmbH, with Reserved Font Name "Bravura". The font is shipped unmodified and
under its own name, which is what a Reserved Font Name allows; a subset or any
other change would have to be renamed. The OFL requires its text to travel with
the font: `NI_UI_MUSIC_FONT_LICENSE` (set by `plugins/_shared/ui/CMakeLists.txt`)
is the path a plugin build that embeds it copies into its bundle.
