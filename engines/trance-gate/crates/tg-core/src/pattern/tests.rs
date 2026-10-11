// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The arrival order: the counting rank against its definition.

use super::*;
use proptest::prelude::*;
use proptest::test_runner::{Config, RngSeed};

/*
 * THE DEFINITION, WRITTEN THE SLOW WAY. A step's rank among the steps of its
 * kind (on or off, inside the length) is one plus the number of them with a
 * lower key, or the same key and a lower index. `renumber` computes it with a
 * histogram; this is the O(n^2) count it replaced, kept as the oracle.
 */
fn renumber_by_definition(p: &mut Pattern) {
    let n = p.length.min(MAX_STEPS);
    let before = p.order;
    for i in 0..n {
        let kind = p.on(i);
        let mut r = 1u32;
        for j in 0..n {
            if j != i && p.on(j) == kind && (before[j] < before[i] || (before[j] == before[i] && j < i)) {
                r += 1;
            }
        }
        p.order[i] = r as u8;
    }
}

proptest! {
    #![proptest_config(Config { cases: 2000, rng_seed: RngSeed::Fixed(0x6f72_6465), failure_persistence: None, ..Config::default() })]

    #[test]
    fn the_counting_rank_is_the_definition(
        steps in any::<u128>(),
        length in 1usize..=MAX_STEPS,
        keys in proptest::collection::vec(any::<u8>(), MAX_STEPS),
    ) {
        let mut p = Pattern::new(0);
        p.steps = Mask::new();
        for i in 0..MAX_STEPS {
            p.steps.set(i, (steps >> i) & 1 != 0);
        }
        p.length = length;
        p.order.copy_from_slice(&keys);
        let mut want = p.clone();
        renumber_by_definition(&mut want);
        p.renumber();
        prop_assert_eq!(&p.order[..length], &want.order[..length]);
        /* Steps past the length keep what they held. */
        prop_assert_eq!(&p.order[length..], &keys[length..]);
    }
}
