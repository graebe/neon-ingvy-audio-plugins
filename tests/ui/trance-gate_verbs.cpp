// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Trance Gate's window verbs, held to the manual (README.md, Slot files
 * and Patch interchange; docs/live.md, Copy and paste, Export and import) and
 * to the iPlug2 shell's words (Patch.cpp), which the manual quotes:
 *
 *   copy and paste   the slot as text through the clipboard; what the engine
 *                    made of a paste, or its reason for refusing
 *   export           the save panel in the last folder with the proposed
 *                    name; the text taken when the panel answers
 *   import           the open panel for both kinds; a NUL is no slot file;
 *                    the engine's refusal in its words
 *   the panels       a cancel says nothing; no panel says so; a folder chosen
 *                    is the next panel's
 *   the row          six icons in the Actions card's order, each named by its
 *                    full verb and its line
 */
#include "Verbs.h"

#include "InfoLines.h"
#include "Outcome.h"
#include "trance-gate_fakes.h"

#include "Info.h"

#include <doctest.h>

using namespace ni::tg;
using namespace ni::tg::test;

namespace
{
struct Rig
{
    FakeModel model;
    FakeClipboard clipboard;
    FakeFilePanels panels;
    std::vector<std::string> said;
    Verbs verbs { model, clipboard, panels, [this] (const std::string& s) { said.push_back (s); } };

    std::string last() const { return said.empty() ? std::string() : said.back(); }
};
} // namespace

/* ------------------------------------------------------------ the row -- */

TEST_CASE ("trance-gate verbs: six icons in the card's order, each named and described")
{
    Rig rig;
    const char* icons[] { "copy", "paste", "export", "export-all", "import", "shuffle" };
    const char* titles[] { "Copy slot", "Paste slot", "Export slot", "Export all", "Import", "Randomize" };
    for (int i = 0; i < (int) Verbs::Verb::count; ++i)
    {
        CAPTURE (i);
        auto& b = rig.verbs.button ((Verbs::Verb) i);
        CHECK (b.isIconOnly());
        CHECK (b.getIcon() == icons[i]);
        CHECK (b.getTitle() == titles[i]);
        /* The line names the verb the icon stands for. */
        CHECK (ni::ui::infoOf (b).startsWith (titles[i]));
        CHECK (ni::ui::infoOf (b).length() <= ni::ui::infoLimit);
    }
    /* Joined edge to edge, sharing hairlines: 28 a button, less one each. */
    CHECK (rig.verbs.group().getWidth() == 6 * 28 - 5);
    CHECK (rig.verbs.group().getHeight() == 28);
}

/* ------------------------------------------------------- the clipboard -- */

TEST_CASE ("trance-gate verbs: Copy puts the slot's text on the clipboard and says which slot")
{
    Rig rig;
    rig.model.set (param::slot, 3.0f);
    rig.verbs.copy();
    CHECK (rig.clipboard.text == rig.model.slotText);
    CHECK (rig.last() == "Copied slot 3.");
}

TEST_CASE ("trance-gate verbs: Copy says why it failed, and leaves the clipboard alone")
{
    Rig rig;
    rig.clipboard.text = "kept";
    rig.model.slotText.clear();
    rig.verbs.copy();
    CHECK (rig.last() == "Failed to copy: the plugin is not ready.");
    CHECK (rig.clipboard.writes == 0);

    rig.model.slotText = "slot";
    rig.clipboard.writable = false;
    rig.verbs.copy();
    CHECK (rig.last() == "Failed to copy: the clipboard could not be written.");
}

TEST_CASE ("trance-gate verbs: Paste hands the clipboard to the engine and says what it replaced")
{
    Rig rig;
    rig.model.set (param::slot, 2.0f);
    rig.clipboard.text = "slot text";
    rig.verbs.paste();
    REQUIRE (rig.model.pasted.size() == 1);
    CHECK (rig.model.pasted.back() == "slot text");
    CHECK (rig.last() == "Pasted into slot 2.");

    rig.model.transferResult = { Transfer::Kind::bank, {} };
    rig.verbs.paste();
    CHECK (rig.last() == "Pasted all 8 slots.");

    rig.model.transferResult = { Transfer::Kind::patch, {} };
    rig.verbs.paste();
    CHECK (rig.last() == "Pasted a whole patch into all 8 slots.");
}

TEST_CASE ("trance-gate verbs: a refused paste says the engine's reason; an empty clipboard is an empty text")
{
    Rig rig;
    rig.model.transferResult = { Transfer::Kind::refused, "The clipboard doesn't hold a Trance Gate slot." };
    rig.verbs.paste();
    CHECK (rig.model.pasted.back().empty());
    CHECK (rig.last() == "Failed to paste: The clipboard doesn't hold a Trance Gate slot.");

    /* No reason from the engine is the words it gives when it cannot say. */
    rig.model.transferResult = { Transfer::Kind::refused, {} };
    rig.verbs.paste();
    CHECK (rig.last() == "Failed to paste: the plugin is not ready.");
}

TEST_CASE ("trance-gate verbs: past the largest text the engine reads, only a byte more is handed over")
{
    Rig rig;
    rig.clipboard.text = std::string ((std::size_t) maxFileBytes * 2, 'x');
    rig.verbs.paste();
    CHECK (rig.model.pasted.back().size() == (std::size_t) maxFileBytes + 1);
}

/* ------------------------------------------------------------ export -- */

TEST_CASE ("trance-gate verbs: Export proposes the slot's file in the last folder, and writes it")
{
    Rig rig;
    rig.model.set (param::slot, 3.0f);
    rig.model.folder = testFolder();
    rig.verbs.exportFile (false);
    REQUIRE (rig.panels.asked.has_value());
    CHECK (rig.panels.asked->save);
    CHECK (rig.panels.asked->name == "NI Trance Gate Slot 3.nitgslot");
    CHECK (rig.panels.asked->patterns == "*.nitgslot");
    CHECK (rig.panels.asked->folder == testFolder());
    CHECK (rig.said.empty());

    /* The text is the slot's when the panel answers, not when it opened. */
    rig.model.slotText = "changed while the panel was open";
    const auto file = testFolder().getChildFile ("sub").getChildFile ("Bassline.nitgslot");
    rig.panels.answer (file);
    CHECK (rig.panels.files[file.getFullPathName()] == "changed while the panel was open");
    CHECK (rig.last() == "Exported slot 3 to Bassline.nitgslot.");
    /* The folder chosen is where the next panel opens. */
    CHECK (rig.model.folder == testFolder().getChildFile ("sub"));
}

TEST_CASE ("trance-gate verbs: Export all writes the bank under its own name")
{
    Rig rig;
    rig.verbs.exportFile (true);
    REQUIRE (rig.panels.asked.has_value());
    CHECK (rig.panels.asked->name == "NI Trance Gate Bank.nitgbank");
    CHECK (rig.panels.asked->patterns == "*.nitgbank");
    const auto file = testFolder().getChildFile ("Set.nitgbank");
    rig.panels.answer (file);
    CHECK (rig.panels.files[file.getFullPathName()] == rig.model.bankText);
    CHECK (rig.last() == "Exported all 8 slots to Set.nitgbank.");
}

TEST_CASE ("trance-gate verbs: an export that cannot be made or written says so; a cancel says nothing")
{
    Rig rig;
    rig.verbs.exportFile (false);
    rig.panels.cancel();
    CHECK (rig.said.empty());
    CHECK (rig.panels.files.empty());

    rig.model.slotText.clear();
    rig.verbs.exportFile (false);
    rig.panels.answer (testFolder().getChildFile ("a.nitgslot"));
    CHECK (rig.last() == "Failed to export: the plugin is not ready.");

    rig.model.slotText = "slot";
    rig.panels.writable = false;
    rig.verbs.exportFile (false);
    rig.panels.answer (testFolder().getChildFile ("b.nitgslot"));
    CHECK (rig.last() == "Failed to write b.nitgslot.");

    rig.panels.canShow = false;
    rig.verbs.exportFile (false);
    CHECK (rig.last() == "Failed to show the save panel.");
}

/* ------------------------------------------------------------ import -- */

TEST_CASE ("trance-gate verbs: Import opens either kind, and the file decides what it replaces")
{
    Rig rig;
    rig.model.set (param::slot, 3.0f);
    const auto slotFile = testFolder().getChildFile ("Bassline.nitgslot");
    rig.panels.files[slotFile.getFullPathName()] = "slot file";
    rig.verbs.import();
    REQUIRE (rig.panels.asked.has_value());
    CHECK_FALSE (rig.panels.asked->save);
    CHECK (rig.panels.asked->patterns == "*.nitgslot;*.nitgbank");
    rig.panels.answer (slotFile);
    CHECK (rig.model.imported.back() == "slot file");
    CHECK (rig.panels.readLimits.back() == (std::size_t) maxFileBytes + 1);
    CHECK (rig.last() == "Imported Bassline.nitgslot into slot 3.");
    CHECK (rig.model.folder == testFolder());

    const auto bankFile = testFolder().getChildFile ("Set.nitgbank");
    rig.panels.files[bankFile.getFullPathName()] = "bank file";
    rig.model.transferResult = { Transfer::Kind::bank, {} };
    rig.verbs.import();
    rig.panels.answer (bankFile);
    CHECK (rig.last() == "Imported all 8 slots from Set.nitgbank.");
}

TEST_CASE ("trance-gate verbs: a file that is not one is refused before the engine, or by it in its words")
{
    Rig rig;
    const auto binary = testFolder().getChildFile ("photo.nitgslot");
    rig.panels.files[binary.getFullPathName()] = std::string ("PNG\0\x01", 5);
    rig.verbs.import();
    rig.panels.answer (binary);
    CHECK (rig.model.imported.empty());
    CHECK (rig.last() == "Failed to import photo.nitgslot: this is not a Trance Gate slot or bank file.");

    const auto newer = testFolder().getChildFile ("newer.nitgslot");
    rig.panels.files[newer.getFullPathName()] = "{\"nitgslot\":99}";
    rig.model.transferResult = { Transfer::Kind::refused, "it was written by a newer version." };
    rig.verbs.import();
    rig.panels.answer (newer);
    CHECK (rig.last() == "Failed to import newer.nitgslot: it was written by a newer version.");

    rig.verbs.import();
    rig.panels.answer (testFolder().getChildFile ("gone.nitgslot"));
    CHECK (rig.last() == "Failed to open gone.nitgslot.");

    rig.verbs.import();
    rig.panels.cancel();
    CHECK (rig.last() == "Failed to open gone.nitgslot.");

    rig.panels.canShow = false;
    rig.verbs.import();
    CHECK (rig.last() == "Failed to show the open panel.");
}

TEST_CASE ("trance-gate verbs: an answer after the verbs are gone is dropped")
{
    FakeModel model;
    FakeClipboard clipboard;
    FakeFilePanels panels;
    int said = 0;
    {
        Verbs verbs { model, clipboard, panels, [&said] (const std::string&) { ++said; } };
        verbs.exportFile (false);
    }
    panels.answer (testFolder().getChildFile ("late.nitgslot"));
    CHECK (said == 0);
    CHECK (panels.files.empty());
}

/* --------------------------------------------------------- the buttons -- */

TEST_CASE ("trance-gate verbs: each button does its verb; Randomize asks the engine and says nothing")
{
    Rig rig;
    rig.verbs.button (Verbs::Verb::copy).onClick();
    CHECK (rig.last() == "Copied slot 1.");
    rig.verbs.button (Verbs::Verb::exportAll).onClick();
    CHECK (rig.panels.asked->name == "NI Trance Gate Bank.nitgbank");
    rig.panels.cancel();
    rig.said.clear();
    rig.verbs.button (Verbs::Verb::randomize).onClick();
    CHECK (rig.model.takeEdits() == "randomize");
    CHECK (rig.said.empty());
}

/* ----------------------------------------------------------- outcomes -- */

TEST_CASE ("trance-gate verbs: an outcome is shown verb first, as the bar splits it")
{
    const auto c = outcome::clause ("Imported Bassline.nitgslot into slot 3.");
    CHECK (c.name == "Imported");
    CHECK (c.rest == "Bassline.nitgslot into slot 3.");
    CHECK (outcome::clause ("Done").rest.isEmpty());
}

/* ------------------------------------------------- the system's files -- */

TEST_CASE ("trance-gate verbs: the system's files are written whole and read back byte for byte, up to a limit")
{
    SystemFilePanels files;
    const juce::TemporaryFile temp (".nitgslot");
    const auto& file = temp.getFile();
    const std::string text ("{\"nitgslot\":1}\nBässe\0end", 24);
    REQUIRE (files.write (file, text));
    CHECK (files.read (file, 1024) == text);
    CHECK (files.read (file, 4) == std::string ("{\"ni"));
    /* Written again, it is replaced, not appended to. */
    REQUIRE (files.write (file, "short"));
    CHECK (files.read (file, 1024) == std::string ("short"));
    CHECK_FALSE (files.read (file.getSiblingFile ("missing.nitgslot"), 16).has_value());
}
