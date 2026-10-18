// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/* The Move's own steps for the two frequencies a Schwung float would turn
 * linearly: read by build.rs, which offers them, and by the module, which
 * reads a value back in the same words (lib.rs). */

/// The Cutoff options' labels, in order -- what `chain_params` declares.
pub const CUTOFF_LABELS: [&str; 12] = ["100", "200", "300", "500", "800", "1.2k", "2k", "3k", "5k", "8k", "12k", "20k"];

/// The Rate options' labels, in order -- what `chain_params` declares.
pub const RATE_LABELS: [&str; 12] = ["250", "500", "1k", "2k", "4k", "6k", "8k", "12k", "16k", "24k", "32k", "48k"];
