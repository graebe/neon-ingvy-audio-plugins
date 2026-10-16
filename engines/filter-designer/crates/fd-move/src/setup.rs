// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/* The module's own starting point, in the order it is set: node 1 a
 * high-pass and node 2 a low-pass, both Butterworth and off until wanted, the
 * isolator on. lib.rs sets the engine to it, and build.rs declares it as
 * those knobs' defaults, so a Delete-to-default lands where a fresh module
 * starts. */

use fd_core::{Field, Param};

pub const SET_UP: [(Param, f64); 9] = [
    (Param::Node(0, Field::Type), 4.0), // High-Pass
    (Param::Node(0, Field::On), 0.0),
    (Param::Node(0, Field::Freq), 30.0),
    (Param::Node(0, Field::Order), 3.0),
    (Param::Node(1, Field::Type), 5.0), // Low-Pass
    (Param::Node(1, Field::On), 0.0),
    (Param::Node(1, Field::Freq), 16_000.0),
    (Param::Node(1, Field::Order), 3.0),
    (Param::IsoOn, 1.0),
];
