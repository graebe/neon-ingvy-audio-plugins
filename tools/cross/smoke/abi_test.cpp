// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

// The plugin's C ABI into Rust, checked without a host: the archive the plugin
// links, called the way its processBlock calls it. A Windows build runs this
// under Wine (CMAKE_CROSSCOMPILING_EMULATOR).

#include "ni_smoke.h"

#include <cstdio>

int main()
{
    float block[] = { 1.0f, -0.5f, 0.25f, 0.0f };
    const float expected[] = { 0.5f, -0.25f, 0.125f, 0.0f };

    ni_smoke_apply_gain (block, 4, 0.5f);
    for (int i = 0; i < 4; ++i)
    {
        if (block[i] != expected[i])
        {
            std::fprintf (stderr, "sample %d is %g, expected %g\n", i, block[i], expected[i]);
            return 1;
        }
    }

    ni_smoke_apply_gain (nullptr, 4, 0.5f);
    std::puts ("ni_smoke_apply_gain: ok");
    return 0;
}
