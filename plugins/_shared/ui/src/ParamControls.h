// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The kit's controls bound to host parameters: the web kit's ParamKnob,
 * ParamSelect and ParamToggle (Param.jsx), once, for every editor.
 *
 * A control turns a number, chooses an index or flips; a ParamBinding says
 * what that means for one parameter. These are the two put together, and
 * nothing else: each shows what the parameter holds -- whoever moved it, the
 * host, automation, a slot recall -- and every edit reaches the host as the
 * gesture ParamBinding.h describes:
 *
 *   ParamKnob    a drag is begin, input per move, end; a key commits; a
 *                double-click resets to the PLUGIN's default; typed text is
 *                read by the plugin (getValueForText)
 *   ParamSelect  a choice commits; a double-click resets to the plugin's
 *                default; the options are the parameter's own choices unless
 *                the editor names them
 *   ParamToggle  a flip commits; on is the upper half of the range, which is
 *                the second of two options -- a switch for a parameter of two
 *                ("two options are a Toggle", Select card)
 *
 * THE TEXT IS THE PARAMETER'S, unless the editor knows better: a readout that
 * shows the value in another unit than the host's (the Trance Gate's stages
 * in milliseconds, which the host lists in percent) is given both directions
 * with setText, and refresh() re-reads it when what it depends on moves.
 *
 * Message thread only.
 */
#pragma once

#include "Knob.h"
#include "ParamBinding.h"
#include "Select.h"
#include "Toggle.h"

#include <functional>
#include <optional>

namespace ni::ui
{

class ParamKnob : public Knob
{
public:
    ParamKnob (juce::RangedAudioParameter&, const juce::String& label);
    ~ParamKnob() override;

    ParamBinding& binding() noexcept { return bound; }

    /* The readout's text, and how typed text is read, when they are not the
     * parameter's: `text` prints the value now, `parse` reads typing into a
     * normalised value or nothing for text it cannot read (which then changes
     * nothing). Either may be empty, to keep the parameter's. */
    using TextSource = std::function<juce::String()>;
    using TextParser = std::function<std::optional<float> (const juce::String&)>;
    void setText (TextSource text, TextParser parse);

    /* Reads the value and the text again. The binding does it on every change
     * of the parameter; an owner whose text source depends on something else
     * calls it when that moves. */
    void refresh();

private:
    void typed (const juce::String&);

    ParamBinding bound;
    TextSource textSource;
    TextParser textParser;

    JUCE_DECLARE_NON_COPYABLE (ParamKnob)
};

class ParamSelect : public Select
{
public:
    /* `options` names the choices in order; empty takes the parameter's own
     * (its getAllValueStrings, which a stepped integer has too). */
    explicit ParamSelect (juce::RangedAudioParameter&, const juce::StringArray& options = {});
    ~ParamSelect() override;

    ParamBinding& binding() noexcept { return bound; }

    /* The option the parameter holds now. */
    int indexOfValue() const;

    void refresh();

private:
    ParamBinding bound;

    JUCE_DECLARE_NON_COPYABLE (ParamSelect)
};

class ParamToggle : public Toggle
{
public:
    ParamToggle (juce::RangedAudioParameter&, const juce::String& label);
    ~ParamToggle() override;

    ParamBinding& binding() noexcept { return bound; }

    void refresh();

private:
    ParamBinding bound;

    JUCE_DECLARE_NON_COPYABLE (ParamToggle)
};

} // namespace ni::ui
