// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The operating system's clipboard. Clipboard.h has what it is for.
 */
#include "Clipboard.h"

namespace ni::tg
{

std::optional<std::string> SystemClipboard::read()
{
    const auto text = juce::SystemClipboard::getTextFromClipboard();
    if (text.isEmpty())
        return std::nullopt;
    return text.toStdString();
}

bool SystemClipboard::write (const std::string& text)
{
    const auto value = juce::String::fromUTF8 (text.data(), (int) text.size());
    juce::SystemClipboard::copyTextToClipboard (value);
    return juce::SystemClipboard::getTextFromClipboard() == value;
}

} // namespace ni::tg
