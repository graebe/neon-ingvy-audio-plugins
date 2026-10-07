// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * One host parameter as a product declares it: a row of its table.
 *
 * PLAIN C++, NO JUCE, so the state codec (Nist.h) and its tests read the same
 * table the processor builds its parameters from (Parameter.h). A product
 * that takes over an iPlug2 build writes its rows as that build declared them
 * -- names, ranges, defaults, units -- because a saved set holds plain values
 * by index and a host shows the names: tests/fixtures/iplug2/<bundle>/
 * parameters.json is what the old build reported, and the products' tests
 * hold the new one to it.
 *
 * THE FOUR KINDS are iPlug2's, which decide how a value is stored and read:
 *
 *   integer      a whole number from min to max, stepped (InitInt)
 *   choice       an index into `texts`, stepped (InitEnum)
 *   toggle       0 or 1, `texts` its two words (InitBool)
 *   continuous   any value from min to max, kept as it comes (InitDouble):
 *                no step, so a host's automation and a saved value are not
 *                snapped to one
 *
 * A stepped value is a whole number wherever it is stored; a continuous one's
 * text is the product's (Parameter.h takes the functions).
 */
#pragma once

namespace ni
{

struct ParamSpec
{
    enum class Kind
    {
        integer,
        choice,
        toggle,
        continuous,
    };

    const char* id;      /* the JUCE parameter ID; with legacy IDs the index is the VST3 ID */
    const char* name;    /* what a host lists */
    Kind kind;
    double min, max, def;  /* plain values, in the parameter's own units */
    const char* units;   /* the VST3 units string ("steps", or "") */
    /* A choice's or a toggle's words, `numTexts` of them; null for a choice
     * whose words the product supplies at run time (an engine's table). */
    const char* const* texts = nullptr;
    int numTexts = 0;

    bool stepped() const noexcept { return kind != Kind::continuous; }
};

} // namespace ni
