# JetBrains Mono, as the native editors embed it

Ultraviolet's one typeface, in the two weights its six text styles use (400
and 500, `uv::tok::type::weights`). The kit embeds both files whole, as
BinaryData of `ni_ui_assets`, and draws every glyph from them: no system font
is ever asked, so a snapshot renders the same text on every machine of an OS.

| File | Weight | SHA-256 |
|---|---|---|
| `JetBrainsMono-Regular.ttf` | 400 | `a0bf60ef0f83c5ed4d7a75d45838548b1f6873372dfac88f71804491898d138f` |
| `JetBrainsMono-Medium.ttf` | 500 | `31c92d01a8a08528b718a43addf0ad3df0af2ca4b7b3290a452f70f358e14d3d` |
| `OFL.txt` | | `30f0c136e3c88e422d0791acd97238870f9054a9729bc34cf2ff0d4ed8cac4ad` |

**Version 2.304**, the official release: `JetBrainsMono-2.304.zip` from
<https://github.com/JetBrains/JetBrainsMono/releases/tag/v2.304>
(SHA-256 `6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf`),
`fonts/ttf/` and `OFL.txt`, unmodified. Each font's `name` table says
"Version 2.304; ttfautohint (v1.8.4.7-5d5b)". They are byte for byte the
files the JUCE editor shipped until September 2026 (commit 7711ba2,
`plugins/trance-gate/Resources/`).

Not subset, unlike the documentation site's WOFF copies (`site/src/uv/fonts`,
made from these by `site/scripts/subset-fonts.sh`): a user-typed name
(Listen-In's bus labels) may use any character the face has, and the full
face keeps that true.

**Licence:** SIL Open Font License 1.1, © 2020 The JetBrains Mono Project
Authors. The OFL requires its text to travel with the font, so `OFL.txt`
stays beside the files here, and a plugin bundle that embeds them ships it
too: `NI_UI_FONT_LICENSE` (set by `plugins/_shared/ui/CMakeLists.txt`) is the
path a plugin build copies into its bundle's resources. JetBrains Mono
declares no Reserved Font Name, so the face keeps its name.
