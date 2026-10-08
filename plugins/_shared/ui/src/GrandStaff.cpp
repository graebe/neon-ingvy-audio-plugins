// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The grand staff. GrandStaff.h has its rules.
 */
#include "GrandStaff.h"

#include "Music.h"
#include "UvTokens.h"
#include "UvType.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;
namespace glyph = music::glyph;

constexpr float half = GrandStaff::space * 0.5f;
/* From the treble's top line to the bass's, in px: E4's line, middle C's
 * ledger and the space either side of it. */
constexpr float staffGap = 7.0f * GrandStaff::space;
constexpr int trebleTopStep = 38;   // F5
constexpr int trebleBottomStep = 30; // E4
constexpr int middleC = 28;
constexpr int bassTopStep = 26;     // A3
constexpr int bassBottomStep = 18;  // G2

/* The signature's accidentals in the order they are written, as letters
 * (0 C to 6 B), and the treble steps they are written on. The bass writes
 * each two octaves lower. */
constexpr int sharpLetters[] = { 3, 0, 4, 1, 5, 2, 6 };
constexpr int sharpSteps[] = { 38, 35, 39, 36, 33, 37, 34 };
constexpr int flatLetters[] = { 6, 2, 5, 1, 4, 0, 3 };
constexpr int flatSteps[] = { 34, 37, 33, 36, 32, 35, 31 };

constexpr float clefX = 4.0f;
constexpr float signatureX = 30.0f;
constexpr float signatureStep = 7.0f;
constexpr float headWidth = 9.5f;
constexpr float ledgerOver = 3.0f;
constexpr float accidentalGap = 3.0f;
constexpr float tailHeight = 2.0f;
constexpr float markTop = 2.0f;

constexpr int whites[] = { 0, 2, 4, 5, 7, 9, 11 };
} // namespace

GrandStaff::GrandStaff (const NoteHistory& h) : HistoryView (h)
{
    setTitle ("Notation");
}

void GrandStaff::setWriter (Writer w)
{
    writer = std::move (w);
    repaint();
}

void GrandStaff::setSignature (int s)
{
    s = std::clamp (s, -7, 7);
    if (s != signature)
    {
        signature = s;
        repaint();
    }
}

GrandStaff::Written GrandStaff::write (int midi) const
{
    if (writer)
        return writer (midi);
    /* Sharps: a black key is the white key below it, raised. */
    const int pc = midi % 12;
    int letter = 6;
    while (whites[letter] > pc)
        --letter;
    return { (midi / 12 - 1) * 7 + letter, pc - whites[letter] };
}

float GrandStaff::trebleTop() const
{
    const float system = staffGap + 4.0f * space;
    return std::max (20.0f, std::round (((float) getHeight() - system) * 0.5f));
}

float GrandStaff::yOfStep (int step) const
{
    if (step >= middleC)
        return trebleTop() + (float) (trebleTopStep - step) * half;
    return trebleTop() + staffGap + (float) (bassTopStep - step) * half;
}

int GrandStaff::signatureOf (int letter) const
{
    const int count = std::abs (signature);
    const int* letters = signature > 0 ? sharpLetters : flatLetters;
    for (int i = 0; i < count; ++i)
        if (letters[i] == letter)
            return signature > 0 ? 1 : -1;
    return 0;
}

float GrandStaff::gutter() const
{
    return signatureX + (float) std::abs (signature) * signatureStep + space;
}

void GrandStaff::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float left = area.getX() + clefX;
    const float right = plot().getRight();

    /* The ten lines. */
    g.setColour (c::line200);
    for (int i = 0; i < 5; ++i)
    {
        g.fillRect (juce::Rectangle<float> (left, yOfStep (trebleBottomStep + 2 * i), right - left, 1.0f));
        g.fillRect (juce::Rectangle<float> (left, yOfStep (bassBottomStep + 2 * i), right - left, 1.0f));
    }
    paintBarLines (g, yOfStep (trebleTopStep), yOfStep (bassBottomStep) + 1.0f);

    /* Clefs on their lines, then the signature. */
    music::drawGlyph (g, glyph::gClef, { left, yOfStep (32) }, space, c::inkMuted);
    music::drawGlyph (g, glyph::fClef, { left, yOfStep (24) }, space, c::inkMuted);
    for (int i = 0; i < std::abs (signature); ++i)
    {
        const bool sharp = signature > 0;
        const int step = (sharp ? sharpSteps : flatSteps)[i];
        const float x = left + signatureX - clefX + (float) i * signatureStep;
        for (const int s : { step, step - 14 })
            music::drawGlyph (g, sharp ? glyph::sharp : glyph::flat, { x, yOfStep (s) }, space, c::inkMuted);
    }

    /* The notes, clipped to the plot. */
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (plot().withY (area.getY()).withHeight (area.getHeight()).toNearestInt());

        /* Where each head goes. Two notes a second apart that start together
         * would overlap, so the upper of each such pair sits a head's width to
         * the right, as an engraver sets them. */
        const double from = windowStart();
        std::vector<std::pair<const NoteHistory::Note*, Written>> live;
        for (const auto& n : history.notes())
            if (n.end >= from)
                live.push_back ({ &n, write (n.midi) });
        std::vector<float> shift (live.size(), 0.0f);
        for (size_t i = 0; i < live.size(); ++i)
            for (size_t j = 0; j < live.size(); ++j)
                if (j != i && juce::exactlyEqual (live[j].first->start, live[i].first->start)
                    && live[i].second.step - live[j].second.step == 1 && juce::exactlyEqual (shift[j], 0.0f)
                    && (live[i].second.step >= middleC) == (live[j].second.step >= middleC))
                    shift[i] = headWidth;

        for (size_t i = 0; i < live.size(); ++i)
        {
            const auto& n = *live[i].first;
            const auto w = live[i].second;
            const float y = yOfStep (w.step);
            const float x0 = xOf (n.start);
            const float x1 = xOf (std::min (n.end, history.clock().now));
            const bool sounding = n.sounding();

            g.setColour (sounding ? c::uvDeep : c::line200);
            g.fillRect (juce::Rectangle<float> (x0, y - tailHeight * 0.5f, std::max (0.0f, x1 - x0), tailHeight));

            if (n.start < from)
                continue;   // its head scrolled out; the tail is all that is left

            /* Ledger lines between the note and its staff. */
            g.setColour (c::line200);
            const auto ledger = [&] (int s) {
                g.fillRect (juce::Rectangle<float> (x0 - ledgerOver, yOfStep (s), headWidth + 2.0f * ledgerOver, 1.0f));
            };
            if (w.step >= middleC)
            {
                for (int s = trebleBottomStep - 2; s >= w.step; s -= 2)
                    ledger (s);
                for (int s = trebleTopStep + 2; s <= w.step; s += 2)
                    ledger (s);
            }
            else
            {
                for (int s = bassBottomStep - 2; s >= w.step; s -= 2)
                    ledger (s);
            }

            const auto ink = sounding ? c::uv : c::inkMuted;
            music::drawGlyph (g, glyph::noteheadBlack, { x0 + shift[i], y }, space, ink);
            if (w.alteration != signatureOf (((w.step % 7) + 7) % 7))
            {
                const auto acc = glyph::accidental (w.alteration);
                music::drawGlyph (g, acc, { x0 - music::glyphWidth (acc, space) - accidentalGap, y }, space, ink);
            }
        }

        /* The names, where each chord began. */
        const auto& hint = uv::tok::type::hint;
        const auto font = uv::type::font (hint);
        const auto& marks = history.marks();
        for (size_t i = 0; i < marks.size(); ++i)
        {
            if (marks[i].at < from)
                continue;
            const float x = xOf (marks[i].at) + 3.0f;
            uv::type::draw (g, marks[i].text, { x, markTop, std::max (0.0f, right - x), hint.lineHeight }, font,
                            i + 1 == marks.size() ? c::ink : c::inkDim);
        }
    }

    paintNow (g);
}

} // namespace ni::ui
