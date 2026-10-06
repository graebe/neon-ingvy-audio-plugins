// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The type: the embedded faces, the six styles at their CSS sizes, and every
 * character the editors write present in both faces -- so no glyph is ever
 * borrowed from a font the machine happens to have.
 */
#include "UvLookAndFeel.h"
#include "UvType.h"

#include <doctest.h>

namespace tok = uv::tok;

namespace
{
/* The faces with no fallback: a character they lack shapes to glyph 0. */
bool hasEveryGlyph (const juce::Typeface::Ptr& face, const juce::String& text)
{
    const juce::Font f (juce::FontOptions (face).withPointHeight (13.0f).withFallbackEnabled (false));
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, text, 0.0f, 0.0f);
    for (int i = 0; i < ga.getNumGlyphs(); ++i)
        if (! ga.getGlyph (i).isWhitespace() && ga.getGlyph (i).getGlyphIndex() == 0)
            return false;
    return ga.getNumGlyphs() > 0;
}
} // namespace

TEST_CASE ("type: both faces are embedded JetBrains Mono, one per weight")
{
    const auto& f = uv::fonts();
    REQUIRE (f.regular != nullptr);
    REQUIRE (f.medium != nullptr);
    CHECK (f.regular->getName().startsWith ("JetBrains Mono"));
    CHECK (f.medium->getName().startsWith ("JetBrains Mono"));
    CHECK (f.regular != f.medium);
    CHECK (f.forWeight (400) == f.regular);
    CHECK (f.forWeight (500) == f.medium);
    CHECK (f.forWeight (700) == f.medium);   // the heaviest the system bundles
}

TEST_CASE ("type: a style's size is the em square, as CSS sizes it")
{
    for (const auto* style : { &tok::type::readout, &tok::type::title, &tok::type::label,
                               &tok::type::value, &tok::type::button, &tok::type::hint })
    {
        const auto f = uv::type::font (*style);
        CHECK (f.getHeightInPoints() == doctest::Approx (style->size));
        CHECK (f.getTypefacePtr() == uv::fonts().forWeight (style->weight));
        /* JetBrains Mono's ascent and descent are 1.32 em: the JUCE height is
         * larger than the CSS size, which is the whole reason for points. */
        CHECK (f.getHeight() > style->size * 1.25f);
    }
}

TEST_CASE ("type: tracking is CSS letter-spacing, in em")
{
    const auto f = uv::type::label();
    const float extraPerGlyph = f.getExtraKerningFactor() * f.getHeight();
    CHECK (extraPerGlyph == doctest::Approx (tok::type::label.tracking * tok::type::label.size));

    /* And it reaches the layout: ten tracked capitals are wider than ten
     * untracked ones by about nine gaps' worth. */
    const auto plain = juce::Font (juce::FontOptions (uv::fonts().medium).withPointHeight (11.0f));
    const float tracked = uv::type::width (f, "ABCDEFGHIJ");
    const float untracked = uv::type::width (plain, "ABCDEFGHIJ");
    CHECK (tracked - untracked >= 9.0f * extraPerGlyph - 0.5f);
    CHECK (tracked - untracked <= 10.0f * extraPerGlyph + 0.5f);
}

TEST_CASE ("type: digits are tabular, so a readout does not jitter")
{
    const auto f = uv::type::value();
    CHECK (uv::type::width (f, "1111") == doctest::Approx (uv::type::width (f, "8888")));
    CHECK (uv::type::width (f, "0.00") == doctest::Approx (uv::type::width (f, "9.99")));
}

TEST_CASE ("type: title and label are set in capitals, the rest as written")
{
    CHECK (uv::type::cased (tok::type::label, "Attack") == "ATTACK");
    CHECK (uv::type::cased (tok::type::title, "Gate") == "GATE");
    CHECK (uv::type::cased (tok::type::value, "32 ms") == "32 ms");
    CHECK (uv::type::isUpper (tok::type::label));
    CHECK_FALSE (uv::type::isUpper (tok::type::hint));
}

TEST_CASE ("type: a value's unit is what follows its last space")
{
    using P = std::pair<juce::String, juce::String>;
    CHECK (uv::type::splitUnit ("40.0 ms") == P { "40.0", "ms" });
    CHECK (uv::type::splitUnit ("90 %") == P { "90", "%" });
    CHECK (uv::type::splitUnit ("1/16") == P { "1/16", "" });
    CHECK (uv::type::splitUnit ("1.00") == P { "1.00", "" });
    CHECK (uv::type::splitUnit ("1 / 16 T") == P { "1 / 16", "T" });
}

TEST_CASE ("type: every character the editors write is in both faces")
{
    /* The printable ASCII, and what the web editors' sources hold beyond it:
     * the dashes and minus of the info lines and values, the middle dot, the
     * arrows of the pad keys, the ellipsis, micro, the command key, the
     * fractions of a rate, and the umlauts of a typed bus name. */
    juce::String text;
    for (juce::juce_wchar ch = 0x21; ch < 0x7f; ++ch)
        text << juce::String::charToString (ch);
    text << juce::String::fromUTF8 ("\xe2\x80\x94\xe2\x80\x93\xe2\x88\x92\xc2\xb7\xe2\x86\x91\xe2\x86\x93"
                                    "\xe2\x86\x90\xe2\x86\x92\xe2\x80\xa6\xc2\xb5\xe2\x8c\x98\xc2\xbd\xc2\xbc"
                                    "\xc2\xbe\xc3\x97\xc2\xb1\xc2\xb0\xc3\xa4\xc3\xb6\xc3\xbc\xc3\x84\xc3\x96"
                                    "\xc3\x9c\xc3\x9f\xc3\xa9");

    CHECK (hasEveryGlyph (uv::fonts().regular, text));
    CHECK (hasEveryGlyph (uv::fonts().medium, text));
}

TEST_CASE ("type: any font JUCE makes for itself is an embedded face")
{
    auto& lnf = juce::LookAndFeel::getDefaultLookAndFeel();
    REQUIRE (dynamic_cast<uv::LookAndFeel*> (&lnf) != nullptr);   // tests/ui/main.cpp

    const juce::Font plain (juce::FontOptions (15.0f));
    CHECK (plain.getTypefacePtr() == uv::fonts().regular);

    const juce::Font bold (juce::FontOptions (15.0f, juce::Font::bold));
    CHECK (bold.getTypefacePtr() == uv::fonts().medium);

    const juce::Font named (juce::FontOptions ("Helvetica", 15.0f, juce::Font::plain));
    CHECK (lnf.getTypefaceForFont (named) == uv::fonts().regular);
}
