// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The JUCE shell's state codec (plugins/_shared/juce/Nist.h) against what the
 * iPlug2 builds actually wrote: tests/fixtures/iplug2, every product.
 *
 *   every fixture    decodes to what the capture decoded it to (its
 *                    <scenario>.json: parameters, strings, bypass, body size)
 *                    and is written back byte for byte, so a set this shell
 *                    saves is one the iPlug2 build reads as its own
 *   the Trance Gate  on its real layout (plugins/trance-gate/Params.cpp): the
 *                    headerless chunks of the builds with 15, 14 and 12
 *                    parameters, a later version's fields, a stepped value
 *                    written whole, and what no build wrote, refused
 *
 * The other three products' layouts are FORMAT.md's, built here from their
 * parameters.json -- each product's own table arrives with its move to the
 * JUCE shell. juce_core reads the JSON; the codec itself is plain C++.
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "Nist.h"
#include "Params.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

namespace
{
const juce::File fixtures { NI_FIXTURES };

Bytes bytesOf (const juce::File& f)
{
    juce::MemoryBlock b;
    REQUIRE_MESSAGE (f.loadFileAsData (b), "missing fixture " << f.getFullPathName());
    const auto* p = static_cast<const std::uint8_t*> (b.getData());
    return Bytes (p, p + b.getSize());
}

/* A product's layout as FORMAT.md gives it, from what its build reported:
 * every parameter below the host's own IDs, a stepped one where stepCount is
 * not 0. The codec reads only a spec's kind and default. */
struct ProductLayout
{
    std::vector<ni::ParamSpec> specs;
    ni::nist::Layout layout;

    const ni::nist::Layout& get()
    {
        if (! specs.empty())
            layout.params = specs.data();
        return layout;
    }
};

ProductLayout layoutOf (const juce::String& product)
{
    if (product == "NITranceGate")
        return { {}, ni::tg::layout() };

    ProductLayout out;
    const auto doc = juce::JSON::parse (fixtures.getChildFile (product + "/parameters.json"));
    if (auto* params = doc["parameters"].getArray())
        for (const auto& p : *params)
            if ((int) p["id"] < 65536)
            {
                const auto kind = (int) p["stepCount"] > 0 ? ni::ParamSpec::Kind::integer : ni::ParamSpec::Kind::continuous;
                out.specs.push_back ({ "", "", kind, 0.0, 1.0, 0.0, "" });
            }
    out.layout.numParams = (int) out.specs.size();
    /* FORMAT.md, per product: the Side-Chain has no strings, the Spectrogram
     * one required and four optional, Listen-In its bus name. */
    if (product == "NISpectrogram")
        out.layout.requiredStrings = 1, out.layout.maxStrings = 5;
    else if (product == "NIListenIn")
        out.layout.requiredStrings = 1, out.layout.maxStrings = 1;
    return out;
}

void putF64 (Bytes& out, double v)
{
    std::uint8_t b[8];
    std::memcpy (b, &v, 8);
    out.insert (out.end(), b, b + 8);
}

void putI32 (Bytes& out, std::int32_t v)
{
    std::uint8_t b[4];
    std::memcpy (b, &v, 4);
    out.insert (out.end(), b, b + 4);
}

void setF64 (Bytes& b, std::size_t at, double v) { std::memcpy (b.data() + at, &v, 8); }
void setI32 (Bytes& b, std::size_t at, std::int32_t v) { std::memcpy (b.data() + at, &v, 4); }

Bytes tgFixture (const char* scenario)
{
    return bytesOf (fixtures.getChildFile (juce::String ("NITranceGate/") + scenario + ".component.bin"));
}

/* A headerless chunk from a Trance Gate build with `count` parameters. */
Bytes tgLegacy (const ni::nist::State& from, int count, bool withBypass = true)
{
    Bytes out;
    for (int i = 0; i < count; ++i)
        putF64 (out, from.params[(std::size_t) i]);
    putI32 (out, (std::int32_t) from.strings.front().size());
    out.insert (out.end(), from.strings.front().begin(), from.strings.front().end());
    if (withBypass)
        putI32 (out, from.bypass.value_or (false) ? 1 : 0);
    return out;
}
} // namespace

TEST_CASE ("every product's fixture decodes as it was captured, and writes back byte for byte")
{
    int seen = 0;
    for (const auto& dir : fixtures.findChildFiles (juce::File::findDirectories, false))
    {
        const auto product = dir.getFileName();
        auto pl = layoutOf (product);
        for (const auto& json : dir.findChildFiles (juce::File::findFiles, false, "*.json"))
        {
            if (json.getFileName() == "parameters.json")
                continue;
            const auto scenario = json.getFileNameWithoutExtension();
            CAPTURE (product);
            CAPTURE (scenario);
            const auto doc = juce::JSON::parse (json)["decoded"];
            const auto saved = bytesOf (dir.getChildFile (scenario + ".component.bin"));
            const auto state = ni::nist::read (pl.get(), saved.data(), saved.size());
            REQUIRE (state.has_value());
            ++seen;
            CHECK_FALSE (state->legacy);
            CHECK (state->bypass == ((int) doc["bypass"] != 0));
            CHECK ((std::size_t) (int) doc["bodyBytes"] + 20 == saved.size());

            auto* params = doc["params"].getArray();
            REQUIRE (params != nullptr);
            REQUIRE ((int) state->params.size() == params->size());
            for (int i = 0; i < params->size(); ++i)
                CHECK (state->params[(std::size_t) i] == doctest::Approx ((double) (*params)[i]).epsilon (1.0e-12));

            auto* strings = doc["strings"].getArray();
            REQUIRE (strings != nullptr);
            REQUIRE ((int) state->strings.size() == strings->size());
            for (int i = 0; i < strings->size(); ++i)
                CHECK (state->strings[(std::size_t) i] == (*strings)[i].toString().toStdString());

            CHECK (ni::nist::write (pl.get(), *state) == saved);
        }
    }
    /* Three Trance Gate scenarios, two of each other product. */
    CHECK (seen == 9);
}

/* ------------------------------------------------------- the Trance Gate -- */

TEST_CASE ("trance-gate: the slots fixture says what its scenario did")
{
    const auto saved = tgFixture ("slots");
    const auto s = ni::nist::read (ni::tg::layout(), saved.data(), saved.size());
    REQUIRE (s.has_value());
    using namespace ni::tg;
    CHECK (s->params[kSlot] == 2.0);
    CHECK (s->params[kLength] == 32.0);
    CHECK (s->params[kWidth] == 50.0);
    CHECK (s->params[kLegato] == 1.0);
    CHECK (s->params[kFade] == 50.0);
    CHECK (s->params[kFadeSoft] == 1.0);
    CHECK (s->params[kFadeDir] == 1.0);
    CHECK (s->strings.front().find ("\"slot\":1,") != std::string::npos);
    CHECK (s->bypass == false);
}

TEST_CASE ("trance-gate: a chunk without the wrapper's bypass reads, and says it has none")
{
    auto saved = tgFixture ("slots");
    saved.resize (saved.size() - 4);
    const auto s = ni::nist::read (ni::tg::layout(), saved.data(), saved.size());
    REQUIRE (s.has_value());
    CHECK_FALSE (s->bypass.has_value());
}

TEST_CASE ("trance-gate: a headerless chunk with today's fifteen reads as the headed one")
{
    const auto saved = tgFixture ("slots");
    const auto headed = ni::nist::read (ni::tg::layout(), saved.data(), saved.size());
    const Bytes legacy (saved.begin() + 16, saved.end());
    const auto s = ni::nist::read (ni::tg::layout(), legacy.data(), legacy.size());
    REQUIRE (s.has_value());
    CHECK (s->legacy);
    CHECK (s->carried == 15);
    CHECK (s->params == headed->params);
    CHECK (s->strings == headed->strings);
    CHECK (s->bypass == headed->bypass);
}

TEST_CASE ("trance-gate: a headerless chunk from before the fade's parameters reads, with their defaults")
{
    const auto saved = tgFixture ("slots");
    const auto headed = ni::nist::read (ni::tg::layout(), saved.data(), saved.size());
    for (const int count : { 12, 14 })
        for (const bool withBypass : { true, false })
        {
            CAPTURE (count);
            CAPTURE (withBypass);
            const auto legacy = tgLegacy (*headed, count, withBypass);
            const auto s = ni::nist::read (ni::tg::layout(), legacy.data(), legacy.size());
            REQUIRE (s.has_value());
            CHECK (s->legacy);
            CHECK (s->carried == count);
            for (int i = 0; i < ni::tg::kNumParams; ++i)
                CHECK (s->params[(std::size_t) i] == (i < count ? headed->params[(std::size_t) i] : ni::tg::specOf (i).def));
            CHECK (s->strings == headed->strings);
            CHECK (s->bypass.has_value() == withBypass);
        }
}

TEST_CASE ("trance-gate: an empty blob is a chunk, which restores no pattern")
{
    auto empty = ni::nist::defaults (ni::tg::layout());
    const auto legacy = tgLegacy (empty, ni::tg::kNumParams);
    const auto s = ni::nist::read (ni::tg::layout(), legacy.data(), legacy.size());
    REQUIRE (s.has_value());
    CHECK (s->strings.front().empty());
}

TEST_CASE ("trance-gate: a later version's extra fields are skipped, and the bypass found after them")
{
    auto saved = tgFixture ("bypassed");
    const auto before = ni::nist::read (ni::tg::layout(), saved.data(), saved.size());
    const std::size_t end = saved.size() - 4;
    saved.insert (saved.begin() + (long) end, { 0xAA, 0xBB, 0xCC, 0xDD });
    std::int32_t body;
    std::memcpy (&body, saved.data() + 12, 4);
    setI32 (saved, 12, body + 4);
    setI32 (saved, 8, 2);
    const auto s = ni::nist::read (ni::tg::layout(), saved.data(), saved.size());
    REQUIRE (s.has_value());
    CHECK (s->strings == before->strings);
    CHECK (s->bypass == true);
}

TEST_CASE ("trance-gate: what no build wrote is refused")
{
    const auto& layout = ni::tg::layout();
    const auto saved = tgFixture ("slots");

    SUBCASE ("nothing")
    {
        CHECK_FALSE (ni::nist::read (layout, nullptr, 0).has_value());
        CHECK_FALSE (ni::nist::read (layout, saved.data(), 0).has_value());
    }
    SUBCASE ("a header whose size runs past the stream")
    {
        auto b = saved;
        setI32 (b, 12, (std::int32_t) b.size());
        CHECK_FALSE (ni::nist::read (layout, b.data(), b.size()).has_value());
    }
    SUBCASE ("a header with a negative size")
    {
        auto b = saved;
        setI32 (b, 12, -1);
        CHECK_FALSE (ni::nist::read (layout, b.data(), b.size()).has_value());
    }
    SUBCASE ("a stepped parameter that is not a whole number")
    {
        auto b = saved;
        setF64 (b, 16 + 8 * ni::tg::kRate, 7.25);
        CHECK_FALSE (ni::nist::read (layout, b.data(), b.size()).has_value());
    }
    SUBCASE ("a parameter that is not a number")
    {
        auto b = saved;
        setF64 (b, 16 + 8 * ni::tg::kAmount, std::numeric_limits<double>::quiet_NaN());
        CHECK_FALSE (ni::nist::read (layout, b.data(), b.size()).has_value());
    }
    SUBCASE ("a blob that runs past the body")
    {
        auto b = saved;
        setI32 (b, 16 + 8 * ni::tg::kNumParams, (std::int32_t) b.size());
        CHECK_FALSE (ni::nist::read (layout, b.data(), b.size()).has_value());
    }
    SUBCASE ("parameters with no blob after them")
    {
        /* The set's fifteen and nothing else. Read as a fourteen-parameter
         * chunk, Fade Dir's 1.0 would be an empty blob and a bypass of
         * 0x3FF00000 -- which no wrapper wrote. */
        const auto s = ni::nist::read (layout, saved.data(), saved.size());
        Bytes b;
        for (const double v : s->params)
            putF64 (b, v);
        CHECK_FALSE (ni::nist::read (layout, b.data(), b.size()).has_value());
    }
    SUBCASE ("a headerless chunk whose bypass is no flag")
    {
        auto legacy = tgLegacy (*ni::nist::read (layout, saved.data(), saved.size()), 15);
        setI32 (legacy, legacy.size() - 4, 7);
        CHECK_FALSE (ni::nist::read (layout, legacy.data(), legacy.size()).has_value());
    }
    SUBCASE ("the JUCE week's state: the bare engine blob, no parameters")
    {
        const auto s = ni::nist::read (layout, saved.data(), saved.size());
        const auto& blob = s->strings.front();
        CHECK_FALSE (ni::nist::read (layout, blob.data(), blob.size()).has_value());
    }
    SUBCASE ("random bytes, a thousand times")
    {
        std::mt19937 rng (20261006);
        int read = 0;
        for (int n = 0; n < 1000; ++n)
        {
            Bytes b ((std::size_t) (rng() % 400) + 1);
            for (auto& x : b)
                x = (std::uint8_t) rng();
            read += ni::nist::read (layout, b.data(), b.size()).has_value() ? 1 : 0;
        }
        CHECK (read == 0);
    }
}

TEST_CASE ("trance-gate: a stepped value is written whole, as iPlug2 stored it")
{
    auto s = ni::nist::defaults (ni::tg::layout());
    s.params[ni::tg::kSlot] = 3.0000001;
    const auto b = ni::nist::write (ni::tg::layout(), s);
    const auto back = ni::nist::read (ni::tg::layout(), b.data(), b.size());
    REQUIRE (back.has_value());
    CHECK (back->params[ni::tg::kSlot] == 3.0);
    CHECK (back->bypass == false);
}

TEST_CASE ("an optional string a chunk does not carry is no string; a required one is the chunk")
{
    /* The Spectrogram's first builds wrote one string; later ones five. */
    auto pl = layoutOf ("NISpectrogram");
    ni::nist::State one;
    one.strings = { "2,5" };
    auto b = ni::nist::write (pl.get(), one);
    /* Cut back to the first string's chunk, as an early build wrote it. */
    const std::size_t firstEnd = 16 + 4 + 3;
    Bytes early (b.begin(), b.begin() + (long) firstEnd);
    setI32 (early, 12, (std::int32_t) (firstEnd - 16));
    putI32 (early, 0);
    const auto s = ni::nist::read (pl.get(), early.data(), early.size());
    REQUIRE (s.has_value());
    CHECK (s->strings == std::vector<std::string> { "2,5" });
    Bytes none (b.begin(), b.begin() + 16);
    setI32 (none, 12, 0);
    CHECK_FALSE (ni::nist::read (pl.get(), none.data(), none.size()).has_value());
}
