// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Every allocation one thread makes, counted -- for proving an audio
 * callback makes none (tests/tg_rt.cpp; first written for the class-ID spike).
 *
 * WHY AN INTERPOSER. The engine's own tests prove the Rust side with a
 * counting global allocator, but a plugin's block is C++ around Rust around
 * whatever the framework calls, and a global allocator sees one language.
 * Every one of them ends in libSystem's malloc family, so that is where this
 * counts: a dylib whose __interpose section dyld applies to the whole process
 * when it is inserted (DYLD_INSERT_LIBRARIES; the test's ENVIRONMENT). Calls
 * made from inside this file reach the real functions -- dyld does not
 * interpose an image on itself.
 *
 * AND THE TYPED FAMILY. Since macOS 14 the system's operator new (libc++abi)
 * allocates through malloc_type_malloc, not malloc: without those, a
 * std::vector growing on the audio thread would count nothing. They are weak
 * imports at this project's deployment target, present on any machine that
 * runs the tests.
 *
 * ONE THREAD, NOT THE PROCESS. Only the thread that called
 * ni_alloc_watch_begin is counted, so a framework thread posting a timer
 * message meanwhile cannot fail the check. Nothing here allocates or locks:
 * an atomic thread handle and an atomic count.
 */
#include <malloc/malloc.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

static _Atomic(pthread_t) gWatched;
static atomic_long gCount;

void ni_alloc_watch_begin(void)
{
  atomic_store(&gCount, 0);
  atomic_store(&gWatched, pthread_self());
}

long ni_alloc_watch_end(void)
{
  atomic_store(&gWatched, (pthread_t) 0);
  return atomic_load(&gCount);
}

static void note(void)
{
  const pthread_t watched = atomic_load(&gWatched);
  if (watched && pthread_equal(watched, pthread_self()))
    atomic_fetch_add(&gCount, 1);
}

static void* counted_malloc(size_t n) { note(); return malloc(n); }
static void* counted_calloc(size_t n, size_t size) { note(); return calloc(n, size); }
static void* counted_realloc(void* p, size_t n) { note(); return realloc(p, n); }
static void* counted_valloc(size_t n) { note(); return valloc(n); }
static void* counted_aligned_alloc(size_t align, size_t n) { note(); return aligned_alloc(align, n); }
static int counted_posix_memalign(void** p, size_t align, size_t n) { note(); return posix_memalign(p, align, n); }
static void* counted_zone_malloc(malloc_zone_t* z, size_t n) { note(); return malloc_zone_malloc(z, n); }
static void* counted_zone_calloc(malloc_zone_t* z, size_t n, size_t size) { note(); return malloc_zone_calloc(z, n, size); }
static void* counted_zone_realloc(malloc_zone_t* z, void* p, size_t n) { note(); return malloc_zone_realloc(z, p, n); }
static void* counted_zone_memalign(malloc_zone_t* z, size_t align, size_t n) { note(); return malloc_zone_memalign(z, align, n); }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"
static void* counted_type_malloc(size_t n, malloc_type_id_t t) { note(); return malloc_type_malloc(n, t); }
static void* counted_type_calloc(size_t n, size_t size, malloc_type_id_t t) { note(); return malloc_type_calloc(n, size, t); }
static void* counted_type_realloc(void* p, size_t n, malloc_type_id_t t) { note(); return malloc_type_realloc(p, n, t); }
static void* counted_type_valloc(size_t n, malloc_type_id_t t) { note(); return malloc_type_valloc(n, t); }
static void* counted_type_aligned_alloc(size_t align, size_t n, malloc_type_id_t t) { note(); return malloc_type_aligned_alloc(align, n, t); }
static int counted_type_posix_memalign(void** p, size_t align, size_t n, malloc_type_id_t t) { note(); return malloc_type_posix_memalign(p, align, n, t); }
static void* counted_type_zone_malloc(malloc_zone_t* z, size_t n, malloc_type_id_t t) { note(); return malloc_type_zone_malloc(z, n, t); }
static void* counted_type_zone_calloc(malloc_zone_t* z, size_t n, size_t size, malloc_type_id_t t) { note(); return malloc_type_zone_calloc(z, n, size, t); }
static void* counted_type_zone_realloc(malloc_zone_t* z, void* p, size_t n, malloc_type_id_t t) { note(); return malloc_type_zone_realloc(z, p, n, t); }
static void* counted_type_zone_memalign(malloc_zone_t* z, size_t align, size_t n, malloc_type_id_t t) { note(); return malloc_type_zone_memalign(z, align, n, t); }

/* dyld's interposing tuples: { replacement, replaced }. */
__attribute__((used)) static const struct
{
  const void* replacement;
  const void* replaced;
} kInterposers[] __attribute__((section("__DATA,__interpose"))) = {
  {(const void*) counted_malloc, (const void*) malloc},
  {(const void*) counted_calloc, (const void*) calloc},
  {(const void*) counted_realloc, (const void*) realloc},
  {(const void*) counted_valloc, (const void*) valloc},
  {(const void*) counted_aligned_alloc, (const void*) aligned_alloc},
  {(const void*) counted_posix_memalign, (const void*) posix_memalign},
  {(const void*) counted_zone_malloc, (const void*) malloc_zone_malloc},
  {(const void*) counted_zone_calloc, (const void*) malloc_zone_calloc},
  {(const void*) counted_zone_realloc, (const void*) malloc_zone_realloc},
  {(const void*) counted_zone_memalign, (const void*) malloc_zone_memalign},
  {(const void*) counted_type_malloc, (const void*) malloc_type_malloc},
  {(const void*) counted_type_calloc, (const void*) malloc_type_calloc},
  {(const void*) counted_type_realloc, (const void*) malloc_type_realloc},
  {(const void*) counted_type_valloc, (const void*) malloc_type_valloc},
  {(const void*) counted_type_aligned_alloc, (const void*) malloc_type_aligned_alloc},
  {(const void*) counted_type_posix_memalign, (const void*) malloc_type_posix_memalign},
  {(const void*) counted_type_zone_malloc, (const void*) malloc_type_zone_malloc},
  {(const void*) counted_type_zone_calloc, (const void*) malloc_type_zone_calloc},
  {(const void*) counted_type_zone_realloc, (const void*) malloc_type_zone_realloc},
  {(const void*) counted_type_zone_memalign, (const void*) malloc_type_zone_memalign},
};
#pragma clang diagnostic pop
