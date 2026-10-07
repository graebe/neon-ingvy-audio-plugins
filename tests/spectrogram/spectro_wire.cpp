// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The text of NI Spectrogram's saved session, read back (Wire.h): the four
 * shapes its strings take, each pinned alone.
 *
 * What a string that does not read does is each reader's to say, because the
 * strings are not alike -- a mangled slot is skipped and the rest kept, a
 * range without its colon is refused whole -- and those rules are what these
 * cases hold. The strings as written are State.cpp's (spectro_state).
 *
 * The web editor's message encoders went with the iPlug2 build: the native
 * editor reads the model, not a wire. ui/test/wire_table.txt stays with ui/
 * for its own decoder's test until the web editors go.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "Wire.h"

#include <clocale>
#include <string>
#include <vector>

using namespace spectro::wire;

TEST_CASE ("a range without a colon is refused and changes neither end")
{
    float lo = 111.f, hi = 222.f;
    CHECK (parse_range ("nonsense", lo, hi) == false);
    CHECK (lo == 111.f);
    CHECK (hi == 222.f);
}

TEST_CASE ("a range splits at the colon")
{
    float lo = 0.f, hi = 0.f;
    REQUIRE (parse_range ("2000:16000", lo, hi));
    CHECK (lo == doctest::Approx (2000.f));
    CHECK (hi == doctest::Approx (16000.f));

    /* The clash's floor and balance take the same shape, sign and all. */
    REQUIRE (parse_range ("-48.50:6.25", lo, hi));
    CHECK (lo == doctest::Approx (-48.5f));
    CHECK (hi == doctest::Approx (6.25f));
}

TEST_CASE ("a range reads '.' as the point whatever the locale")
{
    /* A set saved in Berlin opens in Berlin: the strings are written with
     * '.', and a reader that followed the locale would read 40 for 40.5. */
    const char* was = std::setlocale (LC_NUMERIC, nullptr);
    const std::string saved = was ? was : "C";
    std::setlocale (LC_NUMERIC, "de_DE.UTF-8");
    float lo = 0.f, hi = 0.f;
    REQUIRE (parse_range ("40.50:800.25", lo, hi));
    std::setlocale (LC_NUMERIC, saved.c_str());
    CHECK (lo == doctest::Approx (40.5f));
    CHECK (hi == doctest::Approx (800.25f));
}

TEST_CASE ("a source list skips what it cannot read rather than failing whole")
{
    std::vector<unsigned int> got;
    parse_slots ("2,5,9", got);
    CHECK (got == std::vector<unsigned int> { 2, 5, 9 });

    /* "2,x,5" listens to 2 and 5: a session that silently opens nothing
     * because one field was mangled is the harder fault to see. */
    got.clear();
    parse_slots ("2,x,5", got);
    CHECK (got == std::vector<unsigned int> { 2, 5 });

    got.clear();
    parse_slots ("", got);
    CHECK (got.empty());

    /* Empty fields, and a trailing comma, are nothing rather than a zero --
     * slot 0 is not a bus, it is a mistake. */
    got.clear();
    parse_slots (",,3,", got);
    CHECK (got == std::vector<unsigned int> { 3 });

    got.clear();
    parse_slots ("0,-4,99999", got);
    CHECK (got.empty());
}

TEST_CASE ("channels parse where slots would not, because zero means something")
{
    std::vector<int> got;
    parse_channels ("0,2,3", got);
    CHECK (got == std::vector<int> { 0, 2, 3 });

    /* THE DIFFERENCE FROM parse_slots: channel 0 is the track the plugin sits
     * on, and it is the one a view is most likely to contain. */
    got.clear();
    parse_channels ("0", got);
    CHECK (got == std::vector<int> { 0 });

    /* An unreadable field is skipped, not taken as a zero -- which for
     * channels would silently add this track to a view that did not ask. */
    got.clear();
    parse_channels ("1,x,2", got);
    CHECK (got == std::vector<int> { 1, 2 });

    got.clear();
    parse_channels ("", got);
    CHECK (got.empty());

    got.clear();
    parse_channels ("-1,999", got);
    CHECK (got.empty());
}

TEST_CASE ("a comparison names two channels and whether it is wanted")
{
    int a = -1, b = -1;
    bool on = false;
    REQUIRE (parse_compare ("0:2:1", a, b, on));
    CHECK (a == 0);
    CHECK (b == 2);
    CHECK (on);

    REQUIRE (parse_compare ("1:3:0", a, b, on));
    CHECK (a == 1);
    CHECK (b == 3);
    CHECK (on == false);

    /* Not a range: "0:1:1" through parse_range would be a very small
     * frequency band, which is why this has a reader of its own. A wrong
     * shape, or a negative channel, changes none of the three. */
    a = 111;
    b = 222;
    on = true;
    CHECK (parse_compare ("nonsense", a, b, on) == false);
    CHECK (parse_compare ("0:1", a, b, on) == false);
    CHECK (parse_compare ("-1:2:0", a, b, on) == false);
    CHECK (a == 111);
    CHECK (b == 222);
    CHECK (on);
}
