// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The host's tables, from Schwung's own headers, held to abi/layout.def --
 * the same table src/tests.rs holds ni-schwung's Rust mirrors to. ctest
 * ni_schwung_abi.
 *
 * THE CHECK IS THE COMPILE. Every entry is a _Static_assert, so a vendored
 * header that disagrees with the table does not build, and the message names
 * the field. What runs afterwards only says how much was checked, so a table
 * that lost its entries (an #include of the wrong file, say) cannot pass as
 * an empty success.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "audio_fx_api_v2.h" /* and plugin_api_v1.h, through it */

#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu

#define NI_ABI_FIELD(cs, rs, f, off) \
    _Static_assert(offsetof(cs, f) == (off), \
                   #cs "." #f " is not at +" #off ": the header and abi/layout.def disagree");
#define NI_ABI_SIZE(cs, rs, n) \
    _Static_assert(sizeof(cs) == (n), \
                   "sizeof(" #cs ") is not " #n ": the header and abi/layout.def disagree");
#define NI_ABI_CONST(c, rs, v) \
    _Static_assert((c) == (v), #c " is not " #v ": the header and abi/layout.def disagree");
#include "layout.def"
#undef NI_ABI_FIELD
#undef NI_ABI_SIZE
#undef NI_ABI_CONST

int main(void) {
    int fields = 0, sizes = 0, consts = 0;
#define NI_ABI_FIELD(cs, rs, f, off) fields++;
#define NI_ABI_SIZE(cs, rs, n) sizes++;
#define NI_ABI_CONST(c, rs, v) consts++;
#include "layout.def"
    /* An entry of each kind at least: none means this included some other
     * file of that name, and checked nothing. */
    if (fields == 0 || sizes == 0 || consts == 0) {
        printf("abi/layout.def: %d fields, %d sizes, %d constants -- not the table this checks\n",
               fields, sizes, consts);
        return 1;
    }
    printf("host_api_v1_t and audio_fx_api_v2_t: %d offsets, %d sizes and %d constants\n"
           "  as abi/layout.def has them                                  ok\n",
           fields, sizes, consts);
    return 0;
}

#else

int main(void) {
    /* 77: ctest's skip. The table is LP64; nothing here is a Move build. */
    printf("abi/layout.def is the LP64 layout; this is not an LP64 build -- skipped\n");
    return 77;
}

#endif
