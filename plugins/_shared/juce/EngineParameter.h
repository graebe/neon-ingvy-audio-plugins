// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A host parameter declared from an engine's table: the C++ half of
 * ni-params (engines/shared/crates/ni-params), once, for every product whose
 * C ABI declares its table with it.
 *
 * A product hands over its table as a struct of static functions -- its own
 * `*_param_*` entry points, one line each -- and gets back, per index, the
 * host parameter the table describes:
 *
 *   a number   on the engine's travel (normalise/denormalise), kept to its
 *              resolution (snap) so a value a host keeps as a float comes back
 *              as the number that was set, printed in the engine's words
 *              (text) and read back by the engine (parse) -- text with no
 *              value in it leaves the value where it is
 *   a choice   of the engine's labels; a host may automate it only when the
 *              table says so (NiParamInfo::automatable)
 *
 * The IDs are the engine's keys, version 1.
 *
 * WHAT A TABLE PROVIDES (all static):
 *
 *   using Info = NiParamInfo;                          the product header's
 *   bool info (int, Info*);  bool isChoice (const Info&);
 *   int key (int, char*, int);  int name (int, char*, int);
 *   int label (int, int option, char*, int);  int text (int, double, char*, int);
 *   bool parse (int, const char*, double*);
 *   double normalise (int, double);  double denormalise (int, double);
 *   double snap (int, double);
 *
 * Construction only (the processor's constructor): it allocates.
 */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace ni
{

/* One of the table's texts: key, name, label or a value's. */
template <typename Read>
juce::String engineText (Read read)
{
    char buf[64] {};
    return read (buf, (int) sizeof buf) >= 0 ? juce::String::fromUTF8 (buf) : juce::String();
}

template <typename Table>
class EngineFloat final : public juce::AudioParameterFloat
{
public:
    EngineFloat (int i, const juce::String& key, const juce::String& title, const typename Table::Info& info)
        : juce::AudioParameterFloat (
              juce::ParameterID { key, 1 }, title,
              juce::NormalisableRange<float> (
                  (float) info.min, (float) info.max,
                  [i] (float, float, float n) { return (float) Table::denormalise (i, (double) n); },
                  [i] (float, float, float v) { return (float) Table::normalise (i, (double) v); },
                  [i] (float, float, float v) { return (float) Table::snap (i, (double) v); }),
              (float) info.default_value,
              juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                  [i] (float v, int) { return engineText ([i, v] (char* b, int n) { return Table::text (i, (double) v, b, n); }); })),
          index (i)
    {
    }

    float getValueForText (const juce::String& text) const override
    {
        double v = 0.0;
        if (! Table::parse (index, text.toRawUTF8(), &v))
            return convertTo0to1 (get());
        return convertTo0to1 ((float) v);
    }

private:
    const int index;
};

/* The host parameter the table declares at `index`. */
template <typename Table>
std::unique_ptr<juce::RangedAudioParameter> engineParameter (int index)
{
    typename Table::Info info {};
    const bool known = Table::info (index, &info);
    jassert (known);
    juce::ignoreUnused (known);

    const auto key = engineText ([index] (char* b, int n) { return Table::key (index, b, n); });
    const auto name = engineText ([index] (char* b, int n) { return Table::name (index, b, n); });
    if (Table::isChoice (info))
    {
        juce::StringArray labels;
        for (int option = 0; option <= (int) info.max; ++option)
            labels.add (engineText ([index, option] (char* b, int n) { return Table::label (index, option, b, n); }));
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { key, 1 }, name, labels,
                                                             (int) info.default_value,
                                                             juce::AudioParameterChoiceAttributes().withAutomatable (info.automatable));
    }
    return std::make_unique<EngineFloat<Table>> (index, key, name, info);
}

} // namespace ni
