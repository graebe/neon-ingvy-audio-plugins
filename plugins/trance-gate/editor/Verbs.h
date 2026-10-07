// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Trance Gate's window verbs: Copy slot, Paste slot, Export slot,
 * Export all, Import and Randomize, as one joined group of icons -- the
 * Actions card of Ultraviolet 1.1.0 -- and what each one does.
 *
 * ONE GROUP WHERE THE WEB EDITOR HAD TWO. The web window put Copy and Paste
 * at the end of the settings row, where they ran past the window's edge
 * (canvas D1), and EXPORT, EXPORT ALL and IMPORT as words under the envelope
 * plot, because the system had no glyph for a file then. 1.1.0 has the three
 * glyphs and joins all five verbs edge to edge, in its order -- copy, paste,
 * export, export all, import -- with Randomize after them, as the card puts a
 * window's Randomize. The group stands in the side column right of the
 * panels, flush with their top edge, as the card lays it out; the column is
 * 28 wide, so the icons run top to bottom (ButtonGroup's column), and the
 * settings row keeps only settings. Each icon is named by its full verb (its
 * accessible title) and says what it does in the hint bar (InfoLines.h).
 *
 * WHAT THEY DO, as the iPlug2 shell did it (Patch.cpp, TranceGate.cpp) and
 * the manual says it (README, Slot files and Patch interchange):
 *
 *   copy       the current slot as a slot file's text, onto the clipboard
 *   paste      the clipboard's text to the engine, which decides: a slot
 *              replaces the current one, a bank all eight, a whole patch
 *              everything -- or it refuses, with its reason, and nothing
 *              changes. No text is an empty text, which the engine words
 *   export     the save panel, proposing "NI Trance Gate Slot 3.nitgslot"
 *              (or "NI Trance Gate Bank.nitgbank") in the last folder; the
 *              slot's text is taken when the panel answers, so it is what
 *              the slot holds then
 *   import     the open panel for either kind; the file decides what it
 *              replaces. A file with a NUL in it is not one; past the
 *              largest the engine reads, a byte more than that is handed
 *              over so the engine refuses it in its own words
 *   randomize  a new pattern and arrival order from the engine's generator
 *
 * Every outcome but Randomize's is reported, in the words of Outcome.h, for
 * the hint bar; a cancelled panel says nothing. Every chosen folder becomes
 * the last folder (Model::setLastFolder), so the next panel opens there.
 *
 * A panel answers later. If the editor has gone by then, so has this, and
 * the answer is dropped. Message thread.
 */
#pragma once

#include "Clipboard.h"
#include "FilePanels.h"
#include "Model.h"

#include "Button.h"

#include <array>
#include <functional>
#include <memory>
#include <string>

namespace ni::tg
{

class Verbs final
{
public:
    enum class Verb : int
    {
        copy = 0,
        paste,
        exportSlot,
        exportAll,
        import,
        randomize,
        count
    };

    /* What happened, as one sentence for the hint bar. */
    using Report = std::function<void (const std::string& sentence)>;

    /* Everything given must outlive this. */
    Verbs (Model&, Clipboard&, FilePanels&, Report);
    ~Verbs();

    /* The column of icons, laid out at its own size (idealSize()). */
    ni::ui::ButtonGroup& group() noexcept { return icons; }
    ni::ui::Button& button (Verb v) noexcept { return *buttons[(std::size_t) v]; }

    void copy();
    void paste();
    void exportFile (bool bank);
    void import();
    void randomize();

    /* The current slot, 1..8: the Slot parameter's. */
    int slot() const;

private:
    void report (const std::string& sentence) const;

    Model& model;
    Clipboard& clipboard;
    FilePanels& files;
    Report reportTo;

    std::array<std::unique_ptr<ni::ui::Button>, (std::size_t) Verb::count> buttons;
    ni::ui::ButtonGroup icons { ni::ui::ButtonGroup::Form::column };

    JUCE_DECLARE_WEAK_REFERENCEABLE (Verbs)
    JUCE_DECLARE_NON_COPYABLE (Verbs)
};

} // namespace ni::tg
