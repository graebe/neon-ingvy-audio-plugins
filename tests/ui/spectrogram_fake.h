// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Spectrogram's model, faked: what the plugin would hold, set directly by
 * a test, and every command recorded. The plugin's own Session semantics --
 * an edit is in the session the moment it returns, an empty view is the own
 * channel -- are kept, so the editor is tested against the contract it will
 * meet (Model.h), not against a looser one.
 */
#pragma once

#include "Model.h"
#include "fakes.h"

#include <cmath>
#include <cstring>
#include <deque>
#include <vector>

namespace ni::spectrogram::test
{

class FakeModel final : public Model
{
public:
    FakeModel()
    {
        /* 256 log bands from 10 Hz to 20 kHz, as the analyzer bands them. */
        hz.resize (256);
        for (int i = 0; i < 256; ++i)
            hz[(size_t) i] = (float) (10.0 * std::pow (2000.0, (i + 0.5) / 256.0));
        clock.sampleRate = 48000;
        clock.ppqPerColumn = 1024.0 / 48000.0 * 2.0;
    }

    /* ---- what the plugin holds */
    ni::ui::test::FakeEditorModel base;
    std::vector<float> hz;
    Transport clock;
    std::vector<Source> buses;
    Session look;

    /* Batches waiting for takeColumns: levels, and the clash or empty. */
    struct Batch
    {
        std::vector<std::uint8_t> levels, clash;
        int count = 0;
    };
    std::deque<Batch> pending;

    /* `count` columns of `bands` bytes all `v`, with a clash mask all `c`
     * when `withClash`. */
    void queue (int count, std::uint8_t v, bool withClash = false, std::uint8_t c = 0)
    {
        Batch b;
        b.count = count;
        b.levels.assign ((size_t) count * hz.size(), v);
        if (withClash)
            b.clash.assign (b.levels.size(), c);
        pending.push_back (std::move (b));
    }

    /* One batch of given columns, `count` x the band count, and its mask. */
    void queue (std::vector<std::uint8_t> levels, std::vector<std::uint8_t> clash, int count)
    {
        pending.push_back ({ std::move (levels), std::move (clash), count });
    }

    /* ---- what the editor asked, in order */
    struct Range { float lo, hi; };
    std::vector<Range> ranges;
    struct Look { std::vector<int> view; int a, b; bool clash; std::vector<int> listen; };
    std::vector<Look> looks;
    struct Criteria { float floorDb, balanceDb; };
    std::vector<Criteria> criteria;
    int takes = 0;

    /* ---- EditorModel */
    int numParameters() const override { return 0; }
    juce::RangedAudioParameter& parameter (int i) override { return base.parameter (i); }
    int takeRings (float* s, int capacity) override { return base.takeRings (s, capacity); }
    bool motion() const override { return base.motion(); }
    void setMotion (bool on) override { base.setMotion (on); }

    /* ---- Model */
    int takeColumns (std::uint8_t* levels, std::uint8_t* clash, int capacity, int& clashCount) override
    {
        ++takes;
        clashCount = 0;
        if (pending.empty())
            return 0;
        auto b = std::move (pending.front());
        pending.pop_front();
        const int n = juce::jmin (capacity, b.count);
        std::memcpy (levels, b.levels.data(), (size_t) n * hz.size());
        if (! b.clash.empty())
        {
            std::memcpy (clash, b.clash.data(), (size_t) n * hz.size());
            clashCount = n;
        }
        return n;
    }

    const std::vector<float>& bandCentres() const override { return hz; }
    Transport transport() const override { return clock; }
    const std::vector<Source>& sources() const override { return buses; }
    const Session& session() const override { return look; }

    void setRange (float lo, float hi) override
    {
        ranges.push_back ({ lo, hi });
        look.rangeLo = lo;
        look.rangeHi = hi;
    }

    void setLook (const std::vector<int>& view, int a, int b, bool clash,
                  const std::vector<int>& listen) override
    {
        looks.push_back ({ view, a, b, clash, listen });
        look.view = view.empty() ? std::vector<int> { 0 } : view;
        look.compareA = a;
        look.compareB = b;
        look.clash = clash;
        look.listen = listen;
    }

    void setClashCriteria (float floorDb, float balanceDb) override
    {
        criteria.push_back ({ floorDb, balanceDb });
        look.clashFloorDb = floorDb;
        look.clashBalanceDb = balanceDb;
    }
};

} // namespace ni::spectrogram::test
