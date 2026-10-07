// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What every control in NI Spectrogram does, in one line each, and the
 * window's conventions.
 *
 * The hint bar shows a line while the pointer is over its control or the
 * keyboard focus is visibly on it, and each is also the control's accessible
 * description (ni::ui::setInfo). They are here together so they can be read
 * and edited in one voice. THE FORM: "Name — what it does", at most 72
 * characters, which an InfoText holds at compile time.
 *
 * NEW WITH THE NATIVE EDITOR: the web Spectrogram had no info lines, only the
 * Pause button's tooltip, whose two wordings are its accessible titles here.
 * Each line is written against the user manual (README.md, docs/live.md).
 */
#pragma once

#include "Info.h"

namespace ni::spectrogram::info
{

using ni::ui::InfoText;

/* The first row: how the picture is drawn. */
inline constexpr InfoText range { "Range — zoom the picture to Full, Sub, Bass, Mid or High." };
inline constexpr InfoText span { "Span — seconds of history, or 1 to 16 of the host's bars." };
inline constexpr InfoText pause { "Pause — hold the picture; the analysis keeps running behind it." };
/* The same button while the picture is held: what pressing it does now. */
inline constexpr InfoText resume { "Pause — let the picture go; the history behind it is already current." };

/* The second row: what the picture is of, and what the clash measures. */
inline constexpr InfoText view { "View — tick channels to add into the picture; input is this track." };
inline constexpr InfoText compare { "Compare — the first channel the clash is measured between." };
inline constexpr InfoText against { "Against — the second channel the clash is measured between." };
inline constexpr InfoText clash { "Clash — mark where the two compared channels fight for one place." };

/* The picture itself. */
inline constexpr InfoText picture { "Spectrogram — hover, or the arrow keys, to read frequency, time, level." };

/* The hint bar's Motion switch and Signature say the kit's lines
 * (EditorFrame::motionInfo, signatureInfo), the same in every window. */

/* The accessible titles of the pause button, the web editor's tooltips. */
inline constexpr InfoText pauseTitle { "Pause the picture" };
inline constexpr InfoText resumeTitle { "Resume the picture" };

} // namespace ni::spectrogram::info
