// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The system's file panels and the files they choose. FilePanels.h has the
 * contract.
 */
#include "FilePanels.h"

namespace ni::tg
{

SystemFilePanels::SystemFilePanels() = default;

/* A panel still open goes with its chooser, unanswered: the editor that asked
 * is closing too. */
SystemFilePanels::~SystemFilePanels() = default;

bool SystemFilePanels::show (std::unique_ptr<juce::FileChooser> panel, int flags, Chosen chosen)
{
    if (busy)
        return false;
    /* The last panel's chooser goes here, not in its own answer: a chooser
     * is still on the stack while it calls back. */
    chooser = std::move (panel);
    busy = true;
    juce::WeakReference<SystemFilePanels> self (this);
    chooser->launchAsync (flags, [self, answer = std::move (chosen)] (const juce::FileChooser& c)
    {
        if (self == nullptr)
            return;
        self->busy = false;
        if (answer)
            answer (c.getResult());
    });
    return true;
}

bool SystemFilePanels::save (const juce::File& folder, const juce::String& name, const juce::String& patterns,
                             Chosen chosen)
{
    const auto where = folder.isDirectory() ? folder
                                            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    return show (std::make_unique<juce::FileChooser> ("Export", where.getChildFile (name), patterns),
                 juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting,
                 std::move (chosen));
}

bool SystemFilePanels::open (const juce::File& folder, const juce::String& patterns, Chosen chosen)
{
    return show (std::make_unique<juce::FileChooser> ("Import", folder.isDirectory() ? folder : juce::File(), patterns),
                 juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                 std::move (chosen));
}

bool SystemFilePanels::write (const juce::File& file, const std::string& text)
{
    juce::TemporaryFile temp (file);
    {
        juce::FileOutputStream out (temp.getFile());
        if (! out.openedOk() || ! out.write (text.data(), text.size()))
            return false;
        out.flush();
        if (out.getStatus().failed())
            return false;
    }
    /* Written whole, then put in place: a failed export leaves the file that
     * was there. */
    return temp.overwriteTargetFileWithTemporary();
}

std::optional<std::string> SystemFilePanels::read (const juce::File& file, std::size_t maxBytes)
{
    juce::FileInputStream in (file);
    if (! in.openedOk())
        return std::nullopt;
    std::string text (maxBytes, '\0');
    const auto got = in.read (text.data(), (int) maxBytes);
    if (got < 0)
        return std::nullopt;
    text.resize ((std::size_t) got);
    return text;
}

} // namespace ni::tg
