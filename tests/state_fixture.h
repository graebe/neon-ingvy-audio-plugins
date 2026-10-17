// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A product's saved state, frozen: what every set saved by a released build
 * holds, kept as a file, and loaded by every later build.
 *
 * The products on the shell's own XML state (ni::Processor::writeState) have
 * no older bundle whose chunks were captured, so each keeps one fixture per
 * state version it has released: tests/fixtures/<product>/v<N>.state, saved
 * with a set of values far from the defaults, one in every kind of
 * parameter. The checks, as a host sees them -- by parameter ID, in plain
 * values:
 *
 *   the fixture loads  every listed parameter to its value, every other to
 *                      its default; and so does it without its `state`, as
 *                      every chunk saved before versions were written
 *   a newer version    a chunk whose `state` is above the product's is
 *                      refused, and changes nothing -- and is saved back
 *                      byte for byte, so a set from a newer build loses
 *                      nothing here, until the user edits (a gesture), when
 *                      the instance's own state is saved again
 *   a missing value    a parameter the chunk does not name loads its default,
 *                      whatever the instance held
 *
 * RECORDING. A fixture is written once, when its version is released, by
 * running the test with NI_UPDATE_FIXTURES=1; after that it is never
 * rewritten, so a build that reads it differently fails. A missing fixture
 * fails and says how to record it.
 */
#pragma once

#include "Processor.h"

#include <doctest.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <initializer_list>
#include <memory>
#include <vector>

namespace ni::test
{
struct Setting
{
    const char* id;
    double value;
};

inline juce::RangedAudioParameter* parameterById (juce::AudioProcessor& p, const juce::String& id)
{
    for (auto* each : p.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (each))
            if (ranged->getParameterID() == id)
                return ranged;
    return nullptr;
}

inline double plainOf (const juce::RangedAudioParameter& p)
{
    return (double) p.convertFrom0to1 (p.getValue());
}

inline juce::MemoryBlock saved (juce::AudioProcessor& p)
{
    juce::MemoryBlock m;
    p.getStateInformation (m);
    return m;
}

inline void load (juce::AudioProcessor& p, const juce::MemoryBlock& m)
{
    p.setStateInformation (m.getData(), (int) m.getSize());
}

/* The chunk with its XML changed by `edit`. */
inline juce::MemoryBlock rewritten (const juce::MemoryBlock& chunk, const std::function<void (juce::XmlElement&)>& edit)
{
    auto xml = juce::AudioProcessor::getXmlFromBinary (chunk.getData(), (int) chunk.getSize());
    REQUIRE (xml != nullptr);
    edit (*xml);
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary (*xml, out);
    return out;
}

/*
 * The three checks above for one product: `make` builds a fresh processor,
 * `fixture` is its v<N>.state, `settings` what that fixture was saved with.
 */
inline void checkStateFixture (const std::function<std::unique_ptr<juce::AudioProcessor>()>& make,
                               const juce::File& fixture, std::initializer_list<Setting> settings)
{
    if (juce::SystemStats::getEnvironmentVariable ("NI_UPDATE_FIXTURES", {}).isNotEmpty())
    {
        auto recorder = make();
        for (const auto& s : settings)
        {
            auto* p = parameterById (*recorder, s.id);
            REQUIRE_MESSAGE (p != nullptr, s.id);
            p->setValueNotifyingHost (p->convertTo0to1 ((float) s.value));
        }
        fixture.getParentDirectory().createDirectory();
        const auto chunk = saved (*recorder);
        REQUIRE (fixture.replaceWithData (chunk.getData(), chunk.getSize()));
    }
    juce::MemoryBlock chunk;
    REQUIRE_MESSAGE (fixture.loadFileAsData (chunk),
                     ("no fixture at " + fixture.getFullPathName() + ": record it with NI_UPDATE_FIXTURES=1").toStdString());

    /* The fixture loads: the listed values, every other parameter at its
     * default -- into an instance whose values were all moved first, so a
     * value the chunk did not set would show. */
    auto fresh = make();
    for (auto* each : fresh->getParameters())
        each->setValueNotifyingHost (each->getDefaultValue() < 0.5f ? 1.0f : 0.0f);
    load (*fresh, chunk);
    for (auto* each : fresh->getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (each);
        if (ranged == nullptr)
            continue;
        double want = (double) ranged->convertFrom0to1 (ranged->getDefaultValue());
        for (const auto& s : settings)
            if (ranged->getParameterID() == s.id)
                want = s.value;
        CHECK_MESSAGE (plainOf (*ranged) == doctest::Approx (want).epsilon (1e-6),
                       ranged->getParameterID().toStdString());
    }

    /* A chunk saved before versions were written -- every released build's
     * -- has no `state`, and loads as version 1. */
    auto unversioned = make();
    load (*unversioned, rewritten (chunk, [] (juce::XmlElement& x) { x.removeAttribute ("state"); }));
    CHECK (saved (*unversioned) == saved (*fresh));

    /* A newer version is refused, and changes nothing that plays; saving
     * hands its bytes back as they came, until the user edits. */
    auto later = make();
    std::vector<float> values;
    for (auto* each : later->getParameters())
        values.push_back (each->getValue());
    const auto newer = rewritten (chunk, [] (juce::XmlElement& x) { x.setAttribute ("state", 1000); });
    load (*later, newer);
    std::vector<float> after;
    for (auto* each : later->getParameters())
        after.push_back (each->getValue());
    CHECK (after == values);
    auto* shell = dynamic_cast<ni::Processor*> (later.get());
    REQUIRE (shell != nullptr);
    CHECK (shell->stateUnread());
    CHECK (saved (*later) == newer);
    CHECK (saved (*later) == newer);
    {
        auto* p = later->getParameters()[0];
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue());
        p->endChangeGesture();
    }
    CHECK_FALSE (shell->stateUnread());
    CHECK (saved (*later) != newer);
    /* And a state that reads lets go of one that did not. */
    load (*later, newer);
    CHECK (shell->stateUnread());
    load (*later, chunk);
    CHECK_FALSE (shell->stateUnread());

    /* A missing value loads its default, whatever the instance held. */
    REQUIRE (settings.size() > 0);
    const juce::String gone = settings.begin()->id;
    auto partial = make();
    auto* p = parameterById (*partial, gone);
    REQUIRE (p != nullptr);
    p->setValueNotifyingHost (p->getDefaultValue() < 0.5f ? 1.0f : 0.0f);
    load (*partial, rewritten (chunk, [&] (juce::XmlElement& x) {
        for (auto* e = x.getFirstChildElement(); e != nullptr; e = e->getNextElement())
            if (e->hasTagName ("param") && e->getStringAttribute ("id") == gone)
            {
                x.removeChildElement (e, true);
                break;
            }
    }));
    CHECK (plainOf (*p) == doctest::Approx (p->convertFrom0to1 (p->getDefaultValue())).epsilon (1e-6));
}
} // namespace ni::test
