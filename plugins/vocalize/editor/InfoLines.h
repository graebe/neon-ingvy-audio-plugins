// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The words NI Vocalize's window writes: what every control does, the panels
 * and the plots, and the hint bar's conventions -- together, so they can be
 * read, reviewed and edited in one place, in one voice.
 *
 * Each line is "Name — what it does", the name as the control is labelled, at
 * most 72 characters (an InfoText holds that at compile time), and checked
 * against what the engine does (engines/vocalize/crates/vz-core) and what the
 * manual says (plugins/vocalize/docs/live.md): a wrong line is worse than
 * none.
 */
#pragma once

#include "Info.h"

namespace ni::vz::info
{

using ni::ui::InfoText;

/* THE PANELS: which vowel, how far, and what comes out. */
inline constexpr InfoText vowelPanel { "Vowel — which vowel, and whose voice sings it." };
inline constexpr InfoText vocalPanel { "Vocal — how far the sound has become the vowel, and how." };
inline constexpr InfoText levelPanel { "Level — the vowel against the dry sound, and what comes out." };

/* THE PLOTS. */
inline constexpr InfoText vowelChart { "Vowel — F2 across, F1 down; drop a recording here to capture a voice." };
inline constexpr InfoText envelopePlot { "Envelope — the input's colour in grey, the vowel made of it in front." };

/* VOWEL. */
inline constexpr InfoText vowel { "Vowel — A, E, I, O, U; between two, the way from one to the next." };
inline constexpr InfoText size { "Size — the voice: up is bigger and lower, down smaller and higher." };
inline constexpr InfoText voice { "Voice — from the textbook voice to the one captured; 100 % is all yours." };
inline constexpr InfoText capture { "Capture — learn a voice from a recording of it singing A, E, I, O, U." };
inline constexpr InfoText forget { "Forget — let go of the captured voice; Restore brings it back." };

/* VOCAL. */
inline constexpr InfoText resonance { "Resonance — broad and breathy, or sharp and whistling formants." };
inline constexpr InfoText vocal { "Vocal — how far the sound has become the vowel; 0 % is untouched." };
inline constexpr InfoText excite { "Excite — harmonics for pure sounds, so the vowel has more to shape." };
inline constexpr InfoText mode { "Mode — Time has no latency; Spectral is smoother, one frame late." };

/* LEVEL. */
inline constexpr InfoText mix { "Mix — the vowel against the dry sound; 100 % is all vowel." };
inline constexpr InfoText output { "Output — the level after the mix." };
inline constexpr InfoText ceiling { "Ceiling — bend peaks above -0.3 dBFS, so nothing passes full scale." };

/* Every knob's readout: the engine reads what is typed, in its own units. */
inline constexpr InfoText vowelValue { "Vowel value — type a vowel, such as E, or a way between, E 30." };
inline constexpr InfoText percentValue { "Value — click to type a percentage; Enter sets it." };
inline constexpr InfoText dbValue { "Value — click to type a level in dB, such as -6." };

/* The window's own: the switch and the signature in the hint bar. */
inline constexpr InfoText motion { "Motion — let the beat ripple the background; remembered here." };
inline constexpr InfoText signature { "Neon Ingvy — the publisher of this plugin." };

} // namespace ni::vz::info
