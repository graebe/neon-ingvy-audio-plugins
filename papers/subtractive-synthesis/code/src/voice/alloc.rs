// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*!
Which voice plays a new note. A fixed pool, so a note-on never allocates, and
one rule for when the pool is full: take the voice that will be missed least.
A voice that is free costs nothing; one already releasing is fading anyway;
the oldest held note is the one the ear has had longest. The same note played
again takes its own voice back, so a repeated key never stacks copies.
*/

#[derive(Clone, Copy, Debug, Default)]
pub struct Slot {
    pub note: Option<u8>,
    pub held: bool,
    since: u64,
}

pub struct Allocator<const N: usize> {
    slots: [Slot; N],
    clock: u64,
}

// ANCHOR: alloc
impl<const N: usize> Allocator<N> {
    /// The voice to start `note` on.
    pub fn note_on(&mut self, note: u8) -> usize {
        self.clock += 1;
        let cost = |s: &Slot| match (s.note, s.held) {
            (None, _) => (0, 0),               // free
            (Some(_), false) => (1, s.since),  // releasing: oldest first
            (Some(_), true) => (2, s.since),   // held: oldest first
        };
        let i = self
            .slots
            .iter()
            .position(|s| s.note == Some(note))
            .unwrap_or_else(|| (0..N).min_by_key(|&i| cost(&self.slots[i])).unwrap());
        self.slots[i] = Slot { note: Some(note), held: true, since: self.clock };
        i
    }

    /// The voice to release, if `note` is held.
    pub fn note_off(&mut self, note: u8) -> Option<usize> {
        let i = self.slots.iter().position(|s| s.held && s.note == Some(note))?;
        self.slots[i].held = false;
        Some(i)
    }

    /// The voice's envelope has finished: the slot is free again.
    pub fn free(&mut self, i: usize) {
        self.slots[i] = Slot::default();
    }
}
// ANCHOR_END: alloc

impl<const N: usize> Default for Allocator<N> {
    fn default() -> Self {
        Self { slots: [Slot::default(); N], clock: 0 }
    }
}

impl<const N: usize> Allocator<N> {
    pub fn slots(&self) -> &[Slot; N] {
        &self.slots
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn free_voices_first_then_released_then_the_oldest_held() {
        let mut a = Allocator::<3>::default();
        assert_eq!([a.note_on(60), a.note_on(64), a.note_on(67)], [0, 1, 2]);
        /* Full, all held: the oldest goes. */
        assert_eq!(a.note_on(72), 0);
        /* A released voice is taken before any held one, even a newer one. */
        assert_eq!(a.note_off(67), Some(2));
        assert_eq!(a.note_on(74), 2);
        /* A freed slot before everything. */
        a.free(1);
        assert_eq!(a.note_on(76), 1);
        assert_eq!(a.slots()[1].note, Some(76));
    }

    #[test]
    fn the_same_note_takes_its_own_voice_back() {
        let mut a = Allocator::<4>::default();
        let v = a.note_on(60);
        a.note_on(62);
        a.note_off(60);
        assert_eq!(a.note_on(60), v);
        assert!(a.slots()[v].held);
        assert_eq!(a.note_off(99), None);
    }
}
