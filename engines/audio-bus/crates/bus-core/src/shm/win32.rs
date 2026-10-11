// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Windows: a named file mapping, through windows-sys.
 *
 * A segment is a section backed by the paging file: CreateFileMappingW makes or
 * finds it by name, MapViewOfFile maps it. The name lives in the session's own
 * namespace, "Local\", which is the reach a POSIX name has: the user's own
 * processes. Another user's session cannot see it, let alone open it.
 *
 * A NAME LIVES EXACTLY AS LONG AS A HANDLE TO IT, and that is the one real
 * difference from POSIX. There is no shm_unlink. A named object keeps its name
 * while any process holds a handle and loses it with the last one -- the
 * kernel closes a crashed process's handles too -- and a mapped view keeps
 * the MEMORY alive but not the name. So who keeps a handle decides what the
 * protocol in lib.rs sees:
 *
 *   A WRITER keeps its handle as long as its view. Its claim is what keeps
 *   the bus findable, as a name nobody has unlinked does on POSIX; when the
 *   claim is dropped the handle goes, and the name with it. That is the
 *   unlink.
 *
 *   A READER closes its handle as soon as its view is mapped -- the fd's fate
 *   on POSIX -- so readers never keep a name alive on their own. A sender that
 *   quits takes the name with it, the next one creates a fresh segment, and a
 *   reader moves to it through `Reader::reattach`, exactly as on macOS.
 *
 * Where the answers differ, they differ safely. A claimer that opened a
 * segment just before its owner quit holds a handle of its own, so the name
 * stays on the segment it then claims: POSIX has to notice that orphan and
 * start over (`still_named` in lib.rs), Windows never makes one. And a
 * segment that cannot be interpreted can only be replaced once nobody holds
 * it; until then a claim on that slot is refused.
 */

use core::ptr;

use windows_sys::Win32::Foundation::{
    CloseHandle, GetLastError, ERROR_ALREADY_EXISTS, ERROR_INVALID_PARAMETER, FALSE, HANDLE,
    INVALID_HANDLE_VALUE, STILL_ACTIVE,
};
use windows_sys::Win32::System::Memory::{
    CreateFileMappingW, MapViewOfFile, OpenFileMappingW, UnmapViewOfFile, FILE_MAP,
    FILE_MAP_READ, FILE_MAP_WRITE, MEMORY_MAPPED_VIEW_ADDRESS, PAGE_READWRITE,
};
use windows_sys::Win32::System::Performance::QueryPerformanceCounter;
use windows_sys::Win32::System::Threading::{
    GetExitCodeProcess, OpenProcess, PROCESS_QUERY_LIMITED_INFORMATION,
};

use super::{Name, Refused, NAME_CAP};

/// The session's own namespace. "Global\" would reach every session, and
/// creating there takes a privilege a plugin host does not have.
pub const NAME_PREFIX: &str = "Local\\";

pub struct View {
    base: MEMORY_MAPPED_VIEW_ADDRESS,
    /* The writer's handle, which is what keeps the name; a reader has none. */
    name: Option<HANDLE>,
}

impl View {
    pub fn base(&self) -> *const u8 {
        self.base.Value as *const u8
    }
}

impl Drop for View {
    fn drop(&mut self) {
        unsafe {
            UnmapViewOfFile(self.base);
            if let Some(handle) = self.name {
                CloseHandle(handle);
            }
        }
    }
}

/// The name in UTF-16, NUL-terminated. A Name is ASCII -- a prefix, hex
/// digits and the slot number -- so every byte widens to one code unit.
fn wide(name: &Name) -> [u16; NAME_CAP] {
    let mut out = [0u16; NAME_CAP];
    for (o, &b) in out.iter_mut().zip(name.as_str().as_bytes()) {
        *o = u16::from(b);
    }
    out
}

/*
 * AN EXISTING SECTION KEEPS ITS OWN SIZE, not the one a later open asks for,
 * and one an older build left may be shorter. MapViewOfFile refuses a view
 * that runs past the section's end, so asking for exactly `size` bytes IS the
 * size check: a short segment fails here, on the main thread, and never
 * faults on the audio thread. fstat does this job on POSIX.
 */
fn map(handle: HANDLE, access: FILE_MAP, size: usize) -> Option<MEMORY_MAPPED_VIEW_ADDRESS> {
    let base = unsafe { MapViewOfFile(handle, access, 0, 0, size) };
    (!base.Value.is_null()).then_some(base)
}

/* A shorter section under the name fails to map and is `Failed`, not
 * `Foreign`: Windows cannot take a name from a section somebody holds, so
 * there is nothing a claimer could do with the difference. */
pub fn create_or_open(name: &Name, size: usize) -> Result<(View, bool), Refused> {
    let wide = wide(name);
    let size = size as u64;

    /*
     * ONE CALL DECIDES WHO INITIALISES, as O_EXCL does on POSIX. Under a free
     * name CreateFileMappingW makes the section, zero-filled; under a taken
     * one it hands back the existing section and says ERROR_ALREADY_EXISTS.
     * The kernel settles a race between two claimers, and exactly one of them
     * is told it created.
     */
    let handle = unsafe {
        CreateFileMappingW(
            INVALID_HANDLE_VALUE, /* the paging file, not a file on disk */
            ptr::null(),
            PAGE_READWRITE,
            (size >> 32) as u32,
            size as u32,
            wide.as_ptr(),
        )
    };
    if handle.is_null() {
        return Err(Refused::Failed);
    }
    let created = unsafe { GetLastError() } != ERROR_ALREADY_EXISTS;

    match map(handle, FILE_MAP_READ | FILE_MAP_WRITE, size as usize) {
        Some(base) => Ok((View { base, name: Some(handle) }, created)),
        None => {
            unsafe { CloseHandle(handle) };
            Err(Refused::Failed)
        }
    }
}

pub fn open_existing(name: &Name, size: usize) -> Option<View> {
    let wide = wide(name);
    let handle = unsafe { OpenFileMappingW(FILE_MAP_READ, FALSE, wide.as_ptr()) };
    if handle.is_null() {
        return None;
    }
    let base = map(handle, FILE_MAP_READ, size);
    /* The view keeps the memory. The name is the writer's to keep. */
    unsafe { CloseHandle(handle) };
    Some(View { base: base?, name: None })
}

/// Nothing to do, and nothing that could be done: a name cannot be taken from
/// a section while anybody holds a handle to it. The writer's own handle goes
/// when its view is dropped, and the name with the last handle.
pub fn unlink(_name: &Name) {}

pub fn pid_is_gone(pid: u32) -> bool {
    let process = unsafe { OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) };
    if process.is_null() {
        /* ERROR_INVALID_PARAMETER is "no process has this id", the one answer
         * that proves anything. Access denied is a process we may not look
         * at, which is a live one. */
        return unsafe { GetLastError() } == ERROR_INVALID_PARAMETER;
    }
    let mut code = 0u32;
    let known = unsafe { GetExitCodeProcess(process, &mut code) } != 0;
    unsafe { CloseHandle(process) };
    /* A process that has exited, but that something still holds open, reports
     * its exit code. STILL_ACTIVE is also a code a process can exit with, and
     * then it reads as alive: the conservative mistake, the only kind this
     * check may make. */
    known && code != STILL_ACTIVE as u32
}

pub fn ticks_since_boot() -> u64 {
    let mut ticks = 0i64;
    /* Cannot fail on any Windows a VST3 host runs on. */
    unsafe { QueryPerformanceCounter(&mut ticks) };
    ticks as u64
}
