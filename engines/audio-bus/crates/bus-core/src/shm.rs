// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The shared memory behind a slot: one type, `Shm`, on every OS the plugins
 * ship for.
 *
 * Underneath it is whatever the operating system calls shared memory:
 *
 *   macOS, Linux   POSIX shm_open and mmap, through libc         shm/posix.rs
 *   Windows        a named file mapping, through windows-sys    shm/win32.rs
 *
 * This file is the part that is the same everywhere -- the slot's name, the
 * two doors, what a mapping holds -- so lib.rs writes the claim protocol once
 * and the ring never learns which OS it runs on.
 *
 * The calls used to be declared here by hand, for macOS only, under the
 * zero-dependency rule that docs/adr/0003-established-rust-crates.md reversed.
 * posix.rs tells the two bugs that cost: they are why the declarations now come
 * from crates that know each platform's ABI.
 *
 * WHAT A BACKEND PROVIDES, and promises:
 *
 *   NAME_PREFIX              where the OS keeps shared-memory names
 *   create_or_open(name, n)  a read-write view of at least n bytes, and
 *                            whether THIS call made the segment (zero-filled)
 *   open_existing(name, n)   a read-only view of at least n bytes of a segment
 *                            that exists; it never creates one
 *   unlink(name)             take the name away from its segment
 *   View::base()             the view's first byte, page-aligned; dropping
 *                            the View unmaps it
 *   pid_is_gone(pid)         true only if no process has this pid
 *   ticks_since_boot()       a monotonic count that never repeats within a boot
 *
 * "At least n bytes" is checked BEFORE a view is handed out. A segment that an
 * older build left behind may be shorter, and mapping past its end gives a view
 * whose tail faults on first touch -- on the audio thread, which is the worst
 * possible place to discover it.
 *
 * THE ONE REAL DIFFERENCE IS HOW LONG A NAME LIVES. A POSIX name lives until
 * somebody unlinks it, or the machine reboots. A Windows name lives while some
 * process holds a handle to it, and there is no unlink. win32.rs explains how
 * that is arranged to give lib.rs the answers POSIX gives.
 */

use core::sync::atomic::AtomicU32;

use crate::header::{segment_size, Header, DATA_OFFSET};
use crate::ring::RING_SAMPLES;

#[cfg(any(target_os = "macos", target_os = "linux"))]
mod posix;
#[cfg(any(target_os = "macos", target_os = "linux"))]
use posix as sys;

#[cfg(windows)]
mod win32;
#[cfg(windows)]
use win32 as sys;

/* Every other target is refused here rather than given a backend nobody has
 * run. Android, for one, is Linux without shm_open. */
#[cfg(not(any(target_os = "macos", target_os = "linux", windows)))]
compile_error!("bus-core maps its segments on macOS, Linux and Windows; give shm.rs a backend before building it elsewhere");

/// This process's id, as the owner word records it.
pub fn pid() -> u32 {
    std::process::id()
}

/// Is a process with this pid still around?
///
/// Pids are recycled, so this is only ever used to confirm that a writer is
/// GONE -- never to conclude that one is alive. See `Writer::claim`.
pub fn pid_is_gone(p: u32) -> bool {
    p == 0 || sys::pid_is_gone(p)
}

/// A value no earlier segment under any name has carried: a monotonic clock
/// that starts at boot, and no segment survives a reboot. Two creations under
/// one name are separated by the first segment losing its name, so they cannot
/// share a tick. Only equality is ever asked of it, so its unit is the
/// backend's business.
pub fn incarnation() -> u64 {
    sys::ticks_since_boot().max(1)
}

pub const MAX_SLOT: u32 = 16;

/*
 * "/nia.bus.NN" -- eleven bytes plus the NUL. Windows spells it
 * "Local\nia.bus.NN"; each backend's NAME_PREFIX is the difference.
 *
 * macOS caps a shm name at PSHMNAMLEN (31), and the failure mode for a longer
 * one is ENAMETOOLONG at open time rather than a truncation, so this is
 * comfortable rather than merely sufficient. The name is built without
 * allocating because `claim` is called from the editor thread while audio runs.
 */
const NAME_CAP: usize = 24;

pub struct Name([u8; NAME_CAP]);

/// The environment variable that moves every bus in this process into a
/// private namespace. TESTS ONLY -- see `namespace`.
pub const NAMESPACE_ENV: &str = "NIA_BUS_NS";

/*
 * THE NAMES ARE GLOBAL TO THE USER, AND SO ARE THE TESTS THAT USE THEM.
 *
 * Two checkouts running their suites at once -- or a suite running beside a
 * Live session with Listen-Ins on the same slots -- would claim, unlink and
 * read each other's buses. So a test process can set NIA_BUS_NS, and every
 * name becomes "/nia.XXXXXXXX.NN", the X's a hash of the value: any string
 * picks a private set of sixteen, short enough for PSHMNAMLEN whatever the
 * string was. Unset, the names are the production ones.
 *
 * Read once, on the first name built, and fixed for the life of the process;
 * names are only built by claim, open, probe and release -- main-thread calls
 * -- so the one allocation reading the environment costs is never on the audio
 * thread. A fork inherits it, which is what lets a parent and child test meet.
 */
fn namespace() -> Option<&'static str> {
    static NS: std::sync::OnceLock<Option<String>> = std::sync::OnceLock::new();
    NS.get_or_init(|| {
        let v = std::env::var_os(NAMESPACE_ENV)?;
        let v = v.as_encoded_bytes();
        if v.is_empty() {
            return None;
        }
        Some(format!("{:08x}", fold(v)))
    })
    .as_deref()
}

/*
 * FNV-1a, folded to 32 bits: a namespace, not a secret.
 *
 * Five lines, kept here rather than taken from the fnv crate, because the
 * eight hex digits are part of every name a test process uses, and a name is
 * where two builds meet. `the_names_are_the_format` pins them to FNV's
 * published test vector.
 */
fn fold(v: &[u8]) -> u32 {
    let mut h: u64 = 0xcbf2_9ce4_8422_2325;
    for &b in v {
        h = (h ^ b as u64).wrapping_mul(0x0000_0100_0000_01b3);
    }
    (h ^ (h >> 32)) as u32
}

impl Name {
    pub fn for_slot(slot: u32) -> Name {
        Name::in_namespace(namespace(), slot)
    }

    fn in_namespace(ns: Option<&str>, slot: u32) -> Name {
        let mut buf = [0u8; NAME_CAP];
        let mut n = 0;
        let mut put = |bytes: &[u8]| {
            buf[n..n + bytes.len()].copy_from_slice(bytes);
            n += bytes.len();
        };
        put(sys::NAME_PREFIX.as_bytes());
        match ns {
            None => put(b"nia.bus."),
            Some(ns) => {
                put(b"nia.");
                put(ns.as_bytes());
                put(b".");
            }
        }
        put(&[b'0' + (slot / 10) as u8, b'0' + (slot % 10) as u8]);
        Name(buf)
    }

    pub fn as_str(&self) -> &str {
        let end = self.0.iter().position(|&b| b == 0).unwrap_or(self.0.len());
        core::str::from_utf8(&self.0[..end]).unwrap_or("")
    }
}

/// A slot's segment, mapped: the header, then the ring. Dropping it unmaps it.
pub struct Shm {
    view: sys::View,
    slot: u32,
}

/* Everything reachable through a mapping is an atomic -- the header's fields
 * and the ring's samples alike -- so sharing one between threads is sound in
 * the ordinary way. Which of them may WRITE is the protocol's business (see
 * header.rs), and a reader's mapping cannot: it is read-only. */
unsafe impl Send for Shm {}
unsafe impl Sync for Shm {}

impl Shm {
    /*
     * TWO DOORS, AND ONLY A WRITER MAY USE THE ONE THAT CREATES.
     *
     * The first draft had a single `open` that always passed O_CREAT, and
     * `probe` -- which walks all sixteen slots to build a dropdown -- undid the
     * damage by unlinking anything it had accidentally made. That is a race
     * with a name: probe creates slot 3, a real sender opens it and starts
     * writing, probe then unlinks the NAME, and the next sender creates a
     * different object behind the same name. Two writers, two segments, and
     * readers split silently between them.
     *
     * A reader has no business creating a bus. So it cannot.
     */

    /// Open slot `slot`, creating the segment if nobody has yet. WRITERS ONLY.
    ///
    /// Returns `(segment, created)`. `created` is true for the ONE process
    /// whose open made the segment, which must therefore initialise the header.
    pub fn create_or_open(slot: u32) -> Option<(Shm, bool)> {
        if slot == 0 || slot > MAX_SLOT {
            return None;
        }
        let (view, created) = sys::create_or_open(&Name::for_slot(slot), segment_size())?;
        Some((Shm { view, slot }, created))
    }

    /// Open slot `slot` only if it already exists, READ-ONLY. Readers and
    /// `probe` use this; it never creates anything and cannot write anything,
    /// so a bug in a reader faults in the reader instead of scribbling on a
    /// live bus.
    pub fn open_existing(slot: u32) -> Option<Shm> {
        if slot == 0 || slot > MAX_SLOT {
            return None;
        }
        let view = sys::open_existing(&Name::for_slot(slot), segment_size())?;
        Some(Shm { view, slot })
    }

    pub fn header(&self) -> &Header {
        unsafe { &*(self.view.base() as *const Header) }
    }

    /// The ring. Read-only in a reader's mapping: loads only.
    pub fn data(&self) -> &[AtomicU32] {
        unsafe {
            core::slice::from_raw_parts(
                self.view.base().add(DATA_OFFSET) as *const AtomicU32,
                RING_SAMPLES,
            )
        }
    }

    pub fn slot(&self) -> u32 {
        self.slot
    }

    /// Remove the name, so the next claimer creates a fresh segment. Only the
    /// slot's owner shutting down cleanly, or a claimer replacing a segment it
    /// cannot interpret, does this. On Windows a name cannot be taken from a
    /// segment anybody still holds, and goes by itself with the last handle --
    /// see win32.rs.
    pub fn unlink(&self) {
        sys::unlink(&Name::for_slot(self.slot));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tests_never_touch_the_production_names() {
        /* The workspace's .cargo/config.toml sets NIA_BUS_NS for every cargo
         * test. Without it, this suite would claim and unlink the buses of a
         * Live session running on the same machine. */
        assert!(
            namespace().is_some(),
            "{NAMESPACE_ENV} is not set: the tests would use the real bus names"
        );
        let name = Name::for_slot(7);
        let s = name.as_str();
        let stem = s.strip_prefix(sys::NAME_PREFIX).unwrap_or_default();
        assert!(stem.starts_with("nia.") && stem.ends_with(".07"), "{s}");
        assert_ne!(stem, "nia.bus.07");
        assert!(s.len() <= 31, "past PSHMNAMLEN: {s}");
    }

    #[test]
    fn the_names_are_the_format() {
        /*
         * A SLOT IS ITS NAME. Two builds share a bus only if both spell it the
         * same way, and the plugins installed today speak format v2 under
         * these names -- so they are written out here, not derived. The
         * namespace's hash is FNV-1a 64 folded to 32 bits, and "foobar" is
         * FNV's published test vector: 0x85944171f73967e8, which folds to
         * 0x72ad2699.
         */
        #[cfg(not(windows))]
        let (production, namespaced) = ("/nia.bus.07", "/nia.72ad2699.16");
        #[cfg(windows)]
        let (production, namespaced) = ("Local\\nia.bus.07", "Local\\nia.72ad2699.16");

        assert_eq!(fold(b"foobar"), 0x72ad_2699);
        assert_eq!(Name::in_namespace(None, 7).as_str(), production);
        assert_eq!(Name::in_namespace(Some("72ad2699"), 16).as_str(), namespaced);
    }
}
