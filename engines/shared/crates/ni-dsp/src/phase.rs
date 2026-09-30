/*!
The transport-following phase: a position in units of one step or one cycle,
anchored to the host per block and advanced per sample.

The host's beat position is interpolated per block and its intra-tick fraction
is clamped, so differencing it per sample stutters on every late tick. It is an
anchor that a local accumulator is pulled towards -- a phase-locked loop, not a
clock divider. What a product does at each [`Edge`] stays the product's.
*/

/// Beyond this much error, in units, jump rather than glide.
pub const RESYNC: f64 = 0.25;
/// How fast the loop pulls in, as a time constant, so the pull is the same
/// whatever the block size: the error left after `t` seconds is
/// `exp(-t / TRACK_TAU_S)`. 56.6 ms is 5% per 128-sample block at 44.1 kHz.
pub const TRACK_TAU_S: f64 = 0.0566;
/// The slowest the phase may run while it waits for a host behind it, as a
/// fraction of nominal speed. Never zero: only a resync moves it back.
pub const MIN_SPEED: f64 = 0.5;

/// What the transport did this block.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Edge {
    /// It started: the phase landed exactly on the host's position.
    Started,
    /// Loop, seek or tempo jump: the phase jumped to the host's position.
    Jumped,
    /// Running and close: the phase is gliding towards the host.
    Tracking,
    /// It stopped this block: the phase is parked at 0.
    Stopped,
    /// It was already stopped.
    Parked,
}

#[derive(Clone, Copy, Default, Debug, PartialEq)]
pub struct PhaseTracker {
    /// The position, in units.
    pub pos: f64,
    /// Whether the previous block had a running transport.
    pub was_running: bool,
}

impl PhaseTracker {
    /// Follow the host for one block of `frames` (> 0). `target` is the host's
    /// position in units, `None` when the transport is stopped. Returns the
    /// per-sample increment -- nominal when stopped, never below
    /// `MIN_SPEED` of nominal when running -- and what happened.
    pub fn follow(
        &mut self,
        target: Option<f64>,
        samples_per_unit: f64,
        frames: usize,
        sample_rate: f64,
    ) -> (f64, Edge) {
        let mut inc = 1.0 / samples_per_unit;
        let Some(target) = target else {
            self.pos = 0.0;
            let edge = if self.was_running { Edge::Stopped } else { Edge::Parked };
            self.was_running = false;
            return (inc, edge);
        };
        let edge = if !self.was_running {
            self.pos = target;
            Edge::Started
        } else {
            let err = target - self.pos;
            if err > RESYNC || err < -RESYNC {
                self.pos = target;
                Edge::Jumped
            } else {
                let absorb = 1.0 - (-(frames as f64) / (sample_rate * TRACK_TAU_S)).exp();
                inc += err * absorb / frames as f64;
                Edge::Tracking
            }
        };
        inc = inc.max(MIN_SPEED / samples_per_unit);
        self.was_running = true;
        (inc, edge)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn edges_in_order() {
        let mut p = PhaseTracker::default();
        assert_eq!(p.follow(None, 100.0, 64, 44100.0), (0.01, Edge::Parked));
        assert_eq!(p.follow(Some(3.5), 100.0, 64, 44100.0).1, Edge::Started);
        assert_eq!(p.pos, 3.5);
        let (inc, e) = p.follow(Some(3.6), 100.0, 64, 44100.0);
        assert_eq!(e, Edge::Tracking);
        assert!(inc > 0.01, "a host ahead speeds the phase up");
        assert_eq!(p.pos, 3.5, "tracking moves the increment, not the position");
        assert_eq!(p.follow(Some(9.0), 100.0, 64, 44100.0).1, Edge::Jumped);
        assert_eq!(p.pos, 9.0);
        assert_eq!(p.follow(None, 100.0, 64, 44100.0).1, Edge::Stopped);
        assert_eq!((p.pos, p.was_running), (0.0, false));
    }

    #[test]
    fn never_backwards() {
        let mut p = PhaseTracker::default();
        p.follow(Some(1.0), 100_000.0, 1, 44100.0);
        let (inc, e) = p.follow(Some(0.8), 100_000.0, 1, 44100.0);
        assert_eq!(e, Edge::Tracking);
        assert_eq!(inc, MIN_SPEED / 100_000.0, "a host far behind slows the phase, never reverses it");
    }
}
