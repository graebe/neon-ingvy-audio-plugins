// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Listen-In with nothing behind it: the Bus parameter as the iPlug2 build
 * exposes it, and a bus whose status, peak and name a test sets.
 *
 * THE PARAMETER IS THE PLUGIN'S ONE, AS THE HOST SEES IT: index 0, "Bus", a
 * stepped integer 1..16, default 1, its text the bare number
 * (tests/fixtures/iplug2/NIListenIn/parameters.json on the spike branch:
 * stepCount 15, default normalised 0, display "3").
 *
 * THE NAME IS KEPT AS THE PLUGIN KEEPS IT -- listenin::wire::parse_label,
 * restated: control characters, DEL and colons dropped, then cut to 31 bytes
 * and back to the last whole UTF-8 character -- so a test of what the field
 * shows after a commit tests what the real plugin would hand back.
 */
#pragma once

#include "Model.h"
#include "fakes.h"

#include <deque>
#include <string>

namespace ni::li::test
{

/* listenin::wire::parse_label, over a juce::String. */
inline juce::String keptLabel (const juce::String& typed)
{
    const std::string in = typed.toStdString();
    std::string out;
    for (const char c : in)
    {
        const auto ch = static_cast<unsigned char> (c);
        if (ch < 0x20 || ch == 0x7f || ch == ':')
            continue;
        if ((int) out.size() >= maxLabelBytes)
            break;
        out.push_back ((char) ch);
    }

    /* A trailing partial character goes: find the last lead byte and keep it
     * only if every byte it promises is there. */
    int i = (int) out.size() - 1;
    while (i >= 0 && (((unsigned char) out[(size_t) i]) & 0xc0) == 0x80)
        --i;
    if (i >= 0)
    {
        const auto lead = (unsigned char) out[(size_t) i];
        const int need = (lead & 0x80) == 0x00   ? 1
                         : (lead & 0xe0) == 0xc0 ? 2
                         : (lead & 0xf0) == 0xe0 ? 3
                         : (lead & 0xf8) == 0xf0 ? 4
                                                 : -1;
        if (need < 0 || i + need > (int) out.size())
            out.resize ((size_t) i);
    }
    return juce::String::fromUTF8 (out.data(), (int) out.size());
}

class FakeModel final : public Model
{
public:
    FakeModel()
    {
        params.addInt ("bus", "Bus", 1, numBuses, 1);
    }

    /* ---- EditorModel */
    int numParameters() const override { return params.size(); }
    juce::RangedAudioParameter& parameter (int index) override { return params[index]; }

    int takeRings (float* strengths, int capacity) override
    {
        int n = 0;
        while (n < capacity && ! rings.empty())
        {
            strengths[n++] = rings.front();
            rings.pop_front();
        }
        return n;
    }

    bool motion() const override { return motionOn; }
    void setMotion (bool on) override { motionOn = on; }

    /* ---- Model */
    Status status() const override { return state; }
    float peak() const override { return level; }
    juce::String label() const override { return name; }

    void setLabel (const juce::String& typed) override
    {
        typedLabels.add (typed);
        name = keptLabel (typed);
    }

    /* The bus as the test sets it. */
    void setBus (int bus)
    {
        auto& p = params[param::bus];
        p.setValueNotifyingHost (p.convertTo0to1 ((float) bus));
    }

    int bus() const
    {
        return (int) params[param::bus].convertFrom0to1 (params[param::bus].getValue());
    }

    ni::ui::test::FakeParameters params;
    std::deque<float> rings;
    bool motionOn = true;
    Status state = Status::idle;
    float level = 0.0f;
    juce::String name;

    /* Every setLabel, in order: one per kept edit, never one per keystroke. */
    juce::StringArray typedLabels;
};

} // namespace ni::li::test
