// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the host's window holds: the Trance Gate's editor, and the operating
 * system's clipboard and file panels it uses (editor/Clipboard.h,
 * editor/FilePanels.h), which live exactly as long as it does.
 *
 * The editor sizes itself -- a row of pads at a time with Length, scaled by
 * its own FixedDesign -- and this follows it, as the plugin window
 * (ni::PluginEditor, FollowDesign) follows this. Message thread.
 */
#pragma once

#include "Clipboard.h"
#include "FilePanels.h"
#include "TranceGateEditor.h"

namespace ni::tg
{

class Window final : public juce::Component
{
public:
    explicit Window (Model&);
    ~Window() override;

    TranceGateEditor& editor() noexcept { return gate; }

    void childBoundsChanged (juce::Component*) override;

private:
    SystemClipboard clipboard;
    SystemFilePanels panels;
    TranceGateEditor gate;

    JUCE_DECLARE_NON_COPYABLE (Window)
};

} // namespace ni::tg
