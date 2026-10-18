// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/* The Move's own steps for the knobs a Schwung float would turn linearly --
 * Freq across ten octaves, Q across two decades: read by build.rs, which
 * offers them, and by the module, which reads a value back in the same
 * words (lib.rs). Every product's default is a step. */

/// Freq: the third-octave ladder, 20 Hz to 20 kHz.
pub const FREQ: [&str; 31] = [
    "20", "25", "31.5", "40", "50", "63", "80", "100", "125", "160", "200", "250", "315", "400", "500", "630", "800",
    "1k", "1.25k", "1.6k", "2k", "2.5k", "3.15k", "4k", "5k", "6.3k", "8k", "10k", "12.5k", "16k", "20k",
];

/// Q of a cut, a shelf or an all-pass: past 8 it rings, below 0.5 it sags.
pub const Q_SLOPE: [&str; 12] = ["0.5", "0.6", "0.7071", "0.8", "1", "1.2", "1.4", "2", "2.8", "4", "5.6", "8"];

/// Q of a bell, a band-pass or a notch: a width, from wide to surgical.
pub const Q_WIDTH: [&str; 12] = ["0.3", "0.5", "0.7071", "1", "1.4", "2", "2.8", "4", "5.6", "8", "12", "16"];
