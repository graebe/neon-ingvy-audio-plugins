// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/* The Move's own steps for the four frequencies a Schwung float would turn
 * linearly: read by build.rs, which offers them, and by the module, which
 * reads a value back in the same words (lib.rs). */

/// The frequency knobs, and their options' labels in order.
pub const FREQUENCY_OPTIONS: [(&str, [&str; 12]); 4] = [
    ("iso_freq_lo", ["60", "80", "100", "150", "200", "250", "300", "400", "500", "700", "1k", "1.5k"]),
    ("iso_freq_hi", ["1k", "1.5k", "2k", "2.5k", "3k", "4k", "5k", "6k", "8k", "10k", "12k", "16k"]),
    ("n01_freq", ["20", "30", "40", "50", "60", "80", "100", "150", "200", "300", "500", "1k"]),
    ("n02_freq", ["1k", "2k", "3k", "4k", "5k", "6k", "8k", "10k", "12k", "14k", "16k", "18k"]),
];
