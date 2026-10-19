// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! A node's control step and its run allocate nothing: both happen on an
//! audio callback.

use assert_no_alloc::{assert_no_alloc, AllocDisabler};
use ni_filter::Family;
use ni_filter_node::design::{Channel, Globals, Settings, Shape, Target};
use ni_filter_node::node::{control_in_turn, Budget, Node, SUB};

#[global_allocator]
static A: AllocDisabler = AllocDisabler;

#[test]
fn controlling_and_running_a_node_allocate_nothing() {
    let fs = 48_000.0;
    let at = |shape: Shape, family: Family, channel: Channel| Target {
        settings: Settings {
            shape,
            family,
            order: 5,
            freq: 9000.0,
            gain_db: 4.0,
            q: 2.0,
            ripple_db: 0.5,
            stopband_db: 70.0,
            matched: true,
        },
        channel,
        on: true,
    };
    let mut nodes = [Node::new(&at(Shape::Empty, Family::Butterworth, Channel::Stereo)); 3];
    let (mut l, mut r) = ([0.25; SUB], [-0.25; SUB]);
    let mut first = 0;
    assert_no_alloc(|| {
        for (k, shape) in Shape::ALL.into_iter().cycle().take(40).enumerate() {
            let family = Family::ALL[k % Family::ALL.len()];
            let channel = Channel::ALL[k % Channel::ALL.len()];
            for _ in 0..200 {
                control_in_turn(&mut nodes, &mut first, |_| at(shape, family, channel), 0.1, Globals::NEUTRAL, fs);
                (l, r) = ([0.25; SUB], [-0.25; SUB]);
                for n in &mut nodes {
                    n.run(&mut l, &mut r, 0, 1000.0 / (20.0 * fs), k % 2 == 0);
                    n.flush();
                }
            }
            nodes[0].reset(&at(shape, family, channel), Globals::NEUTRAL, fs);
            let mut b = Budget::UNBOUNDED;
            nodes[1].control(&at(shape, family, channel), 0.1, Globals::NEUTRAL, fs, &mut b);
        }
    });
    assert!(l.iter().chain(r.iter()).all(|x| x.is_finite()));
    assert_eq!(assert_no_alloc::violation_count(), 0);
}
