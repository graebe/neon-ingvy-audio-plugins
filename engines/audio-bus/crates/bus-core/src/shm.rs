/*
 * The POSIX shared-memory mapping, and nothing else.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Six calls, declared here rather than borrowed from libc -- which is
 * MIT/Apache-2.0 and would be perfectly fine, but is a large dependency to
 * take on for `mmap`, and this repository's THIRD_PARTY_LICENSES.md is short
 * on purpose (see bus-core/Cargo.toml).
 *
 * These signatures, the O_* and errno constants and `__error` are macOS's. The
 * plugin is a macOS universal binary and nothing else builds this crate, so any
 * other target is refused at compile time rather than left to link against
 * the wrong numbers.
 */

#[cfg(not(target_os = "macos"))]
compile_error!("bus-core declares macOS's shm/mmap ABI by hand; port shm.rs before building it elsewhere");

use core::sync::atomic::AtomicU32;

use crate::header::{segment_size, Header, DATA_OFFSET};
use crate::ring::RING_SAMPLES;

pub type CInt = i32;

const O_RDONLY: CInt = 0x0000;
const O_RDWR: CInt = 0x0002;
const O_CREAT: CInt = 0x0200;
const O_EXCL: CInt = 0x0800;

const PROT_READ: CInt = 1;
const PROT_WRITE: CInt = 2;
const MAP_SHARED: CInt = 1;
const MAP_FAILED: *mut core::ffi::c_void = usize::MAX as *mut core::ffi::c_void;

const EEXIST: CInt = 17;
const ESRCH: CInt = 3;

extern "C" {
    /*
     * VARIADIC, AND IT HAS TO BE DECLARED THAT WAY.
     *
     * POSIX spells this `int shm_open(const char *, int, ...)`. Declaring the
     * mode as an ordinary third parameter compiles, links, and runs -- and on
     * arm64 it is wrong, because variadic arguments are passed on the STACK
     * while ordinary ones go in registers. The callee reads a mode we never
     * wrote there.
     *
     * The symptom was not a crash. The creating process got its fd and went on
     * happily, having made a segment with permissions 0; every LATER open of
     * that name failed with EACCES, so a sender worked and no reader could ever
     * attach to it. Four tests found it; none of them looked like a calling
     * convention.
     */
    fn shm_open(name: *const u8, oflag: CInt, ...) -> CInt;
    fn shm_unlink(name: *const u8) -> CInt;
    fn ftruncate(fd: CInt, length: i64) -> CInt;
    fn close(fd: CInt) -> CInt;
    fn mmap(
        addr: *mut core::ffi::c_void,
        len: usize,
        prot: CInt,
        flags: CInt,
        fd: CInt,
        offset: i64,
    ) -> *mut core::ffi::c_void;
    fn munmap(addr: *mut core::ffi::c_void, len: usize) -> CInt;
    /*
     * THE 64-BIT-INODE ENTRY POINT, BY NAME. On x86_64 the plain `fstat`
     * symbol is the legacy one that fills the old 32-bit-inode `struct stat`;
     * the layout below is only what `fstat$INODE64` writes. Reading the legacy
     * layout through it put `st_size` in the wrong place, so every open of an
     * existing segment on an Intel Mac failed the size check. arm64 has only
     * the one layout and the one symbol.
     */
    #[cfg_attr(
        all(target_os = "macos", target_arch = "x86_64"),
        link_name = "fstat$INODE64"
    )]
    fn fstat(fd: CInt, buf: *mut Stat) -> CInt;
    fn getpid() -> CInt;
    fn kill(pid: CInt, sig: CInt) -> CInt;
    fn __error() -> *mut CInt;
    fn mach_absolute_time() -> u64;
}

/*
 * macOS `struct stat`, spelled out.
 *
 * The first draft of this read st_size at "offset 96" with a comment admitting
 * it was ugly. An offset in a comment is a layout that no compiler checks:
 * it is right until the day it is silently wrong, and the thing it guards is a
 * mapping whose tail SIGBUSes on the audio thread. So the struct is declared,
 * and `repr(C)` does the arithmetic.
 *
 * This is the 64-bit inode layout (`fstat$INODE64` on x86_64, the only one on
 * arm64); `st_size` is all that is read, the rest is here to place it.
 */
#[repr(C)]
struct Timespec {
    tv_sec: i64,
    tv_nsec: i64,
}

#[repr(C)]
struct Stat {
    st_dev: i32,
    st_mode: u16,
    st_nlink: u16,
    st_ino: u64,
    st_uid: u32,
    st_gid: u32,
    st_rdev: i32,
    st_atime: Timespec,
    st_mtime: Timespec,
    st_ctime: Timespec,
    st_birthtime: Timespec,
    st_size: i64,
    st_blocks: i64,
    st_blksize: i32,
    st_flags: u32,
    st_gen: u32,
    st_lspare: i32,
    st_qspare: [i64; 2],
}

fn errno() -> CInt {
    unsafe { *__error() }
}

pub fn pid() -> u32 {
    unsafe { getpid() as u32 }
}

/// Is a process with this pid still around?
///
/// Pids are recycled, so this is only ever used to confirm that a writer is
/// GONE -- never to conclude that one is alive. See `Writer::claim`.
pub fn pid_is_gone(p: u32) -> bool {
    if p == 0 {
        return true;
    }
    unsafe { kill(p as CInt, 0) == -1 && errno() == ESRCH }
}

/// A value no earlier segment under any name has carried: the monotonic clock,
/// which starts at boot -- and shm does not survive a reboot. Two creations
/// under one name are separated by an unlink, so they cannot share a tick.
pub fn incarnation() -> u64 {
    unsafe { mach_absolute_time() }.max(1)
}

pub const MAX_SLOT: u32 = 16;

/*
 * "/nia.bus.NN" -- eleven bytes plus the NUL.
 *
 * macOS caps a shm name at PSHMNAMLEN (31), and the failure mode for a longer
 * one is ENAMETOOLONG at open time rather than a truncation, so this is
 * comfortable rather than merely sufficient. The name is built without
 * allocating because `claim` is called from the editor thread while audio runs.
 */
pub struct Name([u8; 24]);

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
        /* FNV-1a, folded to 32 bits: a namespace, not a secret. */
        let mut h: u64 = 0xcbf2_9ce4_8422_2325;
        for &b in v {
            h = (h ^ b as u64).wrapping_mul(0x0000_0100_0000_01b3);
        }
        Some(format!("{:08x}", (h ^ (h >> 32)) as u32))
    })
    .as_deref()
}

impl Name {
    pub fn for_slot(slot: u32) -> Name {
        let mut buf = [0u8; 24];
        let mut n = 0;
        let mut put = |bytes: &[u8]| {
            buf[n..n + bytes.len()].copy_from_slice(bytes);
            n += bytes.len();
        };
        match namespace() {
            None => put(b"/nia.bus."),
            Some(ns) => {
                put(b"/nia.");
                put(ns.as_bytes());
                put(b".");
            }
        }
        put(&[b'0' + (slot / 10) as u8, b'0' + (slot % 10) as u8]);
        Name(buf)
    }
    fn as_ptr(&self) -> *const u8 {
        self.0.as_ptr()
    }
    pub fn as_str(&self) -> &str {
        let end = self.0.iter().position(|&b| b == 0).unwrap_or(self.0.len());
        core::str::from_utf8(&self.0[..end]).unwrap_or("")
    }
}

pub struct Mapping {
    base: *mut core::ffi::c_void,
    len: usize,
    slot: u32,
}

/* Everything reachable through a mapping is an atomic -- the header's fields
 * and the ring's samples alike -- so sharing one between threads is sound in
 * the ordinary way. Which of them may WRITE is the protocol's business (see
 * header.rs), and a reader's mapping cannot: it is PROT_READ. */
unsafe impl Send for Mapping {}
unsafe impl Sync for Mapping {}

impl Mapping {
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
    /// Returns `(mapping, created)`. `created` is true for the ONE process that
    /// won `O_CREAT | O_EXCL` and must therefore initialise the header.
    pub fn create_or_open(slot: u32) -> Option<(Mapping, bool)> {
        if slot == 0 || slot > MAX_SLOT {
            return None;
        }
        let name = Name::for_slot(slot);
        let size = segment_size();

        /*
         * O_EXCL DECIDES WHO INITIALISES, and it has to be an atomic decision.
         *
         * The obvious shape -- "open; if it's empty, set it up" -- has two
         * plugins instantiating at once both seeing an empty segment, both
         * ftruncating, and one of them zeroing the ring out from under the
         * other's first block. The kernel can settle this and we cannot, so
         * let it: exactly one O_CREAT|O_EXCL succeeds.
         */
        let mut created = false;
        let mut fd = unsafe { shm_open(name.as_ptr(), O_RDWR | O_CREAT | O_EXCL, 0o600 as CInt) };
        if fd >= 0 {
            created = true;
            if unsafe { ftruncate(fd, size as i64) } != 0 {
                unsafe {
                    close(fd);
                    shm_unlink(name.as_ptr());
                }
                return None;
            }
        } else if errno() == EEXIST {
            fd = unsafe { shm_open(name.as_ptr(), O_RDWR) };
            if fd < 0 {
                return None;
            }
            /*
             * A SEGMENT THAT EXISTS BUT IS THE WRONG SIZE is one an older build
             * left behind, and shm survives until reboot. Mapping `size` bytes
             * over a shorter file gives a mapping whose tail SIGBUSes on first
             * touch -- on the audio thread, which is the worst possible place
             * to discover it. So measure first, and refuse rather than map.
             */
            let mut st: Stat = unsafe { core::mem::zeroed() };
            if unsafe { fstat(fd, &mut st) } != 0 || st.st_size < size as i64 {
                unsafe { close(fd) };
                return None;
            }
        } else {
            return None;
        }

        let base = unsafe {
            mmap(
                core::ptr::null_mut(),
                size,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                fd,
                0,
            )
        };
        /* The fd is not needed once mapped, and leaking one per plugin instance
         * would exhaust the host's table in a large set. */
        unsafe { close(fd) };

        if base == MAP_FAILED || base.is_null() {
            if created {
                unsafe { shm_unlink(name.as_ptr()) };
            }
            return None;
        }

        Some((
            Mapping {
                base,
                len: size,
                slot,
            },
            created,
        ))
    }

    /// Open slot `slot` only if it already exists, READ-ONLY. Readers and
    /// `probe` use this; it never creates anything and cannot write anything,
    /// so a bug in a reader faults in the reader instead of scribbling on a
    /// live bus.
    pub fn open_existing(slot: u32) -> Option<Mapping> {
        if slot == 0 || slot > MAX_SLOT {
            return None;
        }
        let name = Name::for_slot(slot);
        let size = segment_size();

        let fd = unsafe { shm_open(name.as_ptr(), O_RDONLY) };
        if fd < 0 {
            return None;
        }
        let mut st: Stat = unsafe { core::mem::zeroed() };
        if unsafe { fstat(fd, &mut st) } != 0 || st.st_size < size as i64 {
            unsafe { close(fd) };
            return None;
        }
        let base = unsafe { mmap(core::ptr::null_mut(), size, PROT_READ, MAP_SHARED, fd, 0) };
        unsafe { close(fd) };
        if base == MAP_FAILED || base.is_null() {
            return None;
        }
        Some(Mapping {
            base,
            len: size,
            slot,
        })
    }

    pub fn header(&self) -> &Header {
        unsafe { &*(self.base as *const Header) }
    }

    /// The ring. Read-only in a reader's mapping: loads only.
    pub fn data(&self) -> &[AtomicU32] {
        unsafe {
            core::slice::from_raw_parts(
                (self.base as *const u8).add(DATA_OFFSET) as *const AtomicU32,
                RING_SAMPLES,
            )
        }
    }

    pub fn slot(&self) -> u32 {
        self.slot
    }

    /// Remove the name, so the next claimer creates a fresh segment. Only the
    /// slot's owner shutting down cleanly, or a claimer replacing a segment it
    /// cannot interpret, does this.
    pub fn unlink(&self) {
        let name = Name::for_slot(self.slot);
        unsafe { shm_unlink(name.as_ptr()) };
    }
}

impl Drop for Mapping {
    fn drop(&mut self) {
        unsafe { munmap(self.base, self.len) };
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
        assert!(s.starts_with("/nia.") && s.ends_with(".07"), "{s}");
        assert_ne!(s, "/nia.bus.07");
        assert!(s.len() <= 31, "past PSHMNAMLEN: {s}");
    }
}
