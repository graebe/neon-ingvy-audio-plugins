// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * How many times this thread of the kit's test program has allocated: for the tests whose
 * claim is that something allocates NOTHING -- the Spectrogram writing a
 * column, the Ground stepping its field -- so the claim is measured rather
 * than read off the code.
 *
 *   const auto before = ni::ui::test::allocations();
 *   ... the work ...
 *   const auto after = ni::ui::test::allocations();
 *   CHECK (after - before == 0);            // never inside CHECK: it allocates
 *
 * allocations.cpp replaces the global operator new for the whole program
 * (the standard's own mechanism for it) and counts every call made on the
 * CALLING thread -- JUCE's own threads allocate when they like -- so the
 * work under test must be all that runs on it in between: no doctest macro,
 * no juce::String, inside the measured stretch.
 */
#pragma once

namespace ni::ui::test
{

long long allocations() noexcept;

} // namespace ni::ui::test
