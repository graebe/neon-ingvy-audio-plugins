// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The system's save and open panels, and the slot files they choose: what
 * Export slot, Export all and Import use, and all they use.
 *
 * THE EDITOR SHOWS THE PANELS ITSELF, through this, where the iPlug2 shell
 * showed them for the web editor (FileDialog.mm). A panel is asynchronous: it
 * returns at once, and its answer -- the file chosen, or a default File when
 * the person cancelled -- comes later on the message thread, at most once,
 * and never after the FilePanels is gone. The file is read and written here
 * too, so a test needs no disk: the editor's tests answer a panel with a
 * file of their choosing and hold the text "written" to it.
 *
 * SystemFilePanels is juce::FileChooser's, on every platform. Message thread.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ni::tg
{

class FilePanels
{
public:
    virtual ~FilePanels() = default;

    /* The panel's answer: the file, or a default File for a cancel. */
    using Chosen = std::function<void (const juce::File&)>;

    /*
     * The save panel, proposing `name` in `folder` (a default File: the
     * system's choice), for files matching `patterns` ("*.nitgslot"). False
     * when no panel could be shown -- `chosen` is then never called.
     */
    virtual bool save (const juce::File& folder, const juce::String& name, const juce::String& patterns,
                       Chosen chosen) = 0;
    /* The open panel, in `folder`, for files matching `patterns`
     * ("*.nitgslot;*.nitgbank"). */
    virtual bool open (const juce::File& folder, const juce::String& patterns, Chosen chosen) = 0;

    /* Replaces `file` with `text`; false when it could not. */
    virtual bool write (const juce::File& file, const std::string& text) = 0;
    /* At most `maxBytes` of `file`, or nothing when it cannot be read. */
    virtual std::optional<std::string> read (const juce::File& file, std::size_t maxBytes) = 0;
};

class SystemFilePanels final : public FilePanels
{
public:
    SystemFilePanels();
    ~SystemFilePanels() override;

    /* One panel at a time: a second while one is open is not shown. */
    bool save (const juce::File& folder, const juce::String& name, const juce::String& patterns,
               Chosen chosen) override;
    bool open (const juce::File& folder, const juce::String& patterns, Chosen chosen) override;

    bool write (const juce::File& file, const std::string& text) override;
    std::optional<std::string> read (const juce::File& file, std::size_t maxBytes) override;

private:
    bool show (std::unique_ptr<juce::FileChooser>, int flags, Chosen);

    std::unique_ptr<juce::FileChooser> chooser;
    bool busy = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SystemFilePanels)
    JUCE_DECLARE_NON_COPYABLE (SystemFilePanels)
};

} // namespace ni::tg
