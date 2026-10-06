// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

//! The allocator counts what it should, and only while armed.

#[global_allocator]
static A: ni_testkit::Counting = ni_testkit::Counting;

#[test]
fn counts_only_while_armed() {
    let before = vec![1u8; 16];
    ni_testkit::arm();
    let mut v: Vec<u8> = Vec::with_capacity(1);
    v.extend_from_slice(&[0; 64]);
    drop(v);
    ni_testkit::disarm();
    drop(before);
    assert_eq!((ni_testkit::allocs(), ni_testkit::frees()), (2, 1));
}
