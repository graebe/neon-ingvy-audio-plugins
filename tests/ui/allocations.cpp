// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The allocation counter. allocations.h says what it is for.
 *
 * The replaceable global operator new and operator delete ([new.delete]): the
 * plain and array forms, which the nothrow and sized forms call by default.
 * The aligned forms are left to the library, which pairs them with its own
 * aligned delete; a JUCE or kit type over-aligned enough to need them would
 * go uncounted, and none in the work measured here is.
 */
#include "allocations.h"

#include <cstdlib>
#include <new>

namespace
{
/* Per thread: the work under test is on the test's thread, and JUCE's own
 * threads (a timer thread starting up, a font cache) allocate when they
 * like. Constant-initialised, so counting needs no allocation itself. */
thread_local long long count = 0;

void* allocate (std::size_t size)
{
    ++count;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
} // namespace

void* operator new (std::size_t size) { return allocate (size); }
void* operator new[] (std::size_t size) { return allocate (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }

namespace ni::ui::test
{

long long allocations() noexcept
{
    return count;
}

} // namespace ni::ui::test
