// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the hint bar says after a copy, a paste, an export or an import.
 * Outcome.h has where the words come from.
 */
#include "Outcome.h"

namespace ni::tg::outcome
{

namespace
{
/* The engine's reason, or the one it gives when it cannot say. */
std::string reasonOf (const Transfer& t)
{
    return t.reason.empty() ? std::string ("the plugin is not ready.") : t.reason;
}
} // namespace

std::string fileName (bool bank, int slot)
{
    return (bank ? std::string ("NI Trance Gate Bank") : "NI Trance Gate Slot " + std::to_string (slot))
         + "." + extension (bank);
}

std::string extension (bool bank)
{
    return bank ? "nitgbank" : "nitgslot";
}

std::string openPatterns()
{
    return "*." + extension (false) + ";*." + extension (true);
}

std::string copied (int slot)
{
    return "Copied slot " + std::to_string (slot) + ".";
}

std::string copyNotReady()
{
    return "Failed to copy: the plugin is not ready.";
}

std::string copyNotWritten()
{
    return "Failed to copy: the clipboard could not be written.";
}

std::string pasted (const Transfer& t, int slot)
{
    switch (t.kind)
    {
        case Transfer::Kind::slot:  return "Pasted into slot " + std::to_string (slot) + ".";
        case Transfer::Kind::bank:  return "Pasted all 8 slots.";
        case Transfer::Kind::patch: return "Pasted a whole patch into all 8 slots.";
        case Transfer::Kind::refused: break;
    }
    return "Failed to paste: " + reasonOf (t);
}

std::string exported (bool bank, int slot, const std::string& name)
{
    return (bank ? std::string ("Exported all 8 slots") : "Exported slot " + std::to_string (slot))
         + " to " + name + ".";
}

std::string exportNotReady()
{
    return "Failed to export: the plugin is not ready.";
}

std::string notWritten (const std::string& name)
{
    return "Failed to write " + name + ".";
}

std::string imported (const Transfer& t, int slot, const std::string& name)
{
    switch (t.kind)
    {
        case Transfer::Kind::slot:
            return "Imported " + name + " into slot " + std::to_string (slot) + ".";
        /* A file is never a patch; were the engine to take one, it replaced
         * all eight, which is what the bank's words say. */
        case Transfer::Kind::bank:
        case Transfer::Kind::patch:
            return "Imported all 8 slots from " + name + ".";
        case Transfer::Kind::refused:
            break;
    }
    return "Failed to import " + name + ": " + reasonOf (t);
}

std::string notOpened (const std::string& name)
{
    return "Failed to open " + name + ".";
}

std::string notASlotFile (const std::string& name)
{
    return "Failed to import " + name + ": this is not a Trance Gate slot or bank file.";
}

std::string noSavePanel()
{
    return "Failed to show the save panel.";
}

std::string noOpenPanel()
{
    return "Failed to show the open panel.";
}

ni::ui::Clause clause (const std::string& sentence)
{
    const auto text = juce::String::fromUTF8 (sentence.c_str(), (int) sentence.size());
    const int space = text.indexOfChar (' ');
    if (space < 0)
        return { text, {} };
    return { text.substring (0, space), text.substring (space + 1) };
}

} // namespace ni::tg::outcome
