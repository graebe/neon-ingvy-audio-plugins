// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The clipboard, as the Trance Gate's Copy slot and Paste into slot use it.
 *
 * THE EDITOR READS AND WRITES IT ITSELF, through this, and only through this.
 * In the web editor the plugin did (a WebView in a host can neither read the
 * clipboard nor receive ⌘V); natively the editor can, and the model only
 * turns a slot into text and text back into slots (Model::exportText, paste).
 * Live keeps ⌘C and ⌘V for its own menu either way, which is why the window
 * has the two icons at all.
 *
 * An interface so the editor's tests can watch it: a test's clipboard holds a
 * string, or none, or refuses to be written. SystemClipboard is the
 * operating system's. Message thread.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <string>

namespace ni::tg
{

class Clipboard
{
public:
    virtual ~Clipboard() = default;

    /* The text on the clipboard, as UTF-8; nothing when it holds no text. */
    virtual std::optional<std::string> read() = 0;
    /* Puts `text` on the clipboard; false when it could not. */
    virtual bool write (const std::string& text) = 0;
};

/* juce::SystemClipboard, on every platform. */
class SystemClipboard final : public Clipboard
{
public:
    std::optional<std::string> read() override;

    /* juce::SystemClipboard reports no failure, so the write is read back:
     * a clipboard that does not hold what was put on it was not written. */
    bool write (const std::string& text) override;
};

} // namespace ni::tg
