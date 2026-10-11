// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * POSIX shared memory -- macOS and Linux -- through libc.
 *
 * A segment is a shm object: shm_open makes or finds it by name, ftruncate
 * sizes it, mmap maps it, and the name stays until shm_unlink or a reboot.
 *
 * libc's declarations carry two things this file once got wrong by hand, and
 * both are why it no longer declares anything itself:
 *
 *   shm_open IS VARIADIC ON macOS: `int shm_open(const char *, int, ...)`.
 *   Declaring the mode as an ordinary third parameter compiled, linked and ran
 *   -- and on arm64, where variadic arguments are passed on the stack and
 *   ordinary ones in registers, the callee read a mode nobody had written.
 *   The symptom was not a crash. The creating process got its fd and went on
 *   happily, having made a segment with permissions 0; every LATER open of
 *   that name failed with EACCES, so a sender worked and no reader could ever
 *   attach to it. libc declares it variadic there (and not on Linux, where it
 *   is not), and Rust refuses to pass anything narrower than a C int through
 *   `...` -- hence MODE's type.
 *
 *   AN INTEL MAC HAS TWO `fstat`s. The plain symbol fills the old
 *   32-bit-inode `struct stat`; read through the 64-bit layout, `st_size` was
 *   in the wrong place, and every open of an existing segment on an Intel Mac
 *   failed the size check. libc links `fstat$INODE64` there and declares the
 *   struct it fills, so the compiler places `st_size` and no comment does.
 */

use core::ffi::{c_int, c_uint, c_void, CStr};

use super::{Name, Refused};

/// A POSIX shm name is one leading slash and no other.
pub const NAME_PREFIX: &str = "/";

/* Owner read and write: only the user's own processes may open a bus (the
 * README's trust model). A c_uint, not a mode_t, because macOS passes it
 * through shm_open's `...`; Linux's mode_t is the same type. An open without
 * O_CREAT ignores it, but Linux's shm_open takes it either way. */
const MODE: c_uint = 0o600;

pub struct View {
    base: *mut c_void,
    len: usize,
}

impl View {
    pub fn base(&self) -> *const u8 {
        self.base as *const u8
    }
}

impl Drop for View {
    fn drop(&mut self) {
        unsafe { libc::munmap(self.base, self.len) };
    }
}

fn path(name: &Name) -> &CStr {
    CStr::from_bytes_until_nul(&name.0).expect("a Name always ends in NUL")
}

fn errno() -> c_int {
    std::io::Error::last_os_error().raw_os_error().unwrap_or(0)
}

/*
 * A SEGMENT THAT EXISTS BUT IS THE WRONG SIZE is one an older build left
 * behind, and shm survives until reboot. Mapping `size` bytes over a shorter
 * object gives a mapping whose tail SIGBUSes on first touch. So measure first,
 * and refuse rather than map.
 */
fn holds(fd: c_int, size: usize) -> bool {
    let mut st: libc::stat = unsafe { core::mem::zeroed() };
    let measured = unsafe { libc::fstat(fd, &mut st) } == 0;
    measured && st.st_size >= size as libc::off_t
}

/// Map `size` bytes of `fd` and close it, whether or not the map worked.
fn map(fd: c_int, size: usize, prot: c_int) -> Option<View> {
    let base = unsafe { libc::mmap(core::ptr::null_mut(), size, prot, libc::MAP_SHARED, fd, 0) };
    /* The fd is not needed once mapped, and leaking one per plugin instance
     * would exhaust the host's table in a large set. */
    unsafe { libc::close(fd) };
    if base == libc::MAP_FAILED || base.is_null() {
        return None;
    }
    Some(View { base, len: size })
}

pub fn create_or_open(name: &Name, size: usize) -> Result<(View, bool), Refused> {
    let path = path(name);

    /*
     * O_EXCL DECIDES WHO INITIALISES, and it has to be an atomic decision.
     *
     * The obvious shape -- "open; if it's empty, set it up" -- has two
     * plugins instantiating at once both seeing an empty segment, both
     * ftruncating, and one of them zeroing the ring out from under the
     * other's first block. The kernel can settle this and we cannot, so
     * let it: exactly one O_CREAT|O_EXCL succeeds.
     */
    let fd = unsafe { libc::shm_open(path.as_ptr(), libc::O_RDWR | libc::O_CREAT | libc::O_EXCL, MODE) };
    if fd >= 0 {
        if unsafe { libc::ftruncate(fd, size as libc::off_t) } != 0 {
            unsafe {
                libc::close(fd);
                libc::shm_unlink(path.as_ptr());
            }
            return Err(Refused::Failed);
        }
        let Some(view) = map(fd, size, libc::PROT_READ | libc::PROT_WRITE) else {
            unsafe { libc::shm_unlink(path.as_ptr()) };
            return Err(Refused::Failed);
        };
        return Ok((view, true));
    }
    if errno() != libc::EEXIST {
        return Err(Refused::Failed);
    }

    let fd = unsafe { libc::shm_open(path.as_ptr(), libc::O_RDWR, MODE) };
    if fd < 0 {
        return Err(Refused::Failed);
    }
    /* Too short is an older format's segment, which the claimer replaces --
     * see Refused::Foreign. */
    if !holds(fd, size) {
        unsafe { libc::close(fd) };
        return Err(Refused::Foreign);
    }
    map(fd, size, libc::PROT_READ | libc::PROT_WRITE)
        .map(|v| (v, false))
        .ok_or(Refused::Failed)
}

pub fn open_existing(name: &Name, size: usize) -> Option<View> {
    let fd = unsafe { libc::shm_open(path(name).as_ptr(), libc::O_RDONLY, MODE) };
    if fd < 0 {
        return None;
    }
    if !holds(fd, size) {
        unsafe { libc::close(fd) };
        return None;
    }
    map(fd, size, libc::PROT_READ)
}

pub fn unlink(name: &Name) {
    unsafe { libc::shm_unlink(path(name).as_ptr()) };
}

pub fn pid_is_gone(pid: u32) -> bool {
    /* kill() reads a negative pid as a process GROUP. Nothing past i32::MAX is
     * a process, so an owner word that large -- a foreign or damaged segment --
     * names no live writer. */
    let Ok(pid) = libc::pid_t::try_from(pid) else {
        return true;
    };
    /* Signal 0 delivers nothing; it only asks whether the pid exists. ESRCH is
     * the one answer that proves anything: EPERM is a process that is alive
     * and simply not ours to signal. */
    unsafe { libc::kill(pid, 0) == -1 && errno() == libc::ESRCH }
}

/* macOS: CLOCK_UPTIME_RAW is mach_absolute_time's clock, which this crate has
 * always read, in nanoseconds; libc deprecates mach_absolute_time itself in
 * favour of a crate of its own. Linux: CLOCK_MONOTONIC. Both count from boot,
 * and /dev/shm and macOS's shm are both gone after a reboot. */
#[cfg(target_os = "macos")]
const BOOT_CLOCK: libc::clockid_t = libc::CLOCK_UPTIME_RAW;
#[cfg(target_os = "linux")]
const BOOT_CLOCK: libc::clockid_t = libc::CLOCK_MONOTONIC;

pub fn ticks_since_boot() -> u64 {
    let mut now: libc::timespec = unsafe { core::mem::zeroed() };
    /* Cannot fail: the clock exists and the pointer is ours. */
    unsafe { libc::clock_gettime(BOOT_CLOCK, &mut now) };
    (now.tv_sec as u64)
        .wrapping_mul(1_000_000_000)
        .wrapping_add(now.tv_nsec as u64)
}
