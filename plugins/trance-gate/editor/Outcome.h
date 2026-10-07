// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the hint bar says after a copy, a paste, an export or an import --
 * every outcome, word for word, in one place.
 *
 * THE WORDS ARE THE PLUGIN'S, as the manual quotes them (README.md, Slot
 * files and Copy and paste; docs/live.md): the iPlug2 shell wrote them in
 * Patch.cpp and TranceGate.cpp and sent them to the web editor, which showed
 * them. Here the editor does the work itself -- it holds the clipboard and
 * the file panels (Clipboard.h, FilePanels.h; Verbs.h does the work), and the
 * model hands it the slot's text and what the engine made of a text
 * (Model::exportText, importText, paste) -- so the wording moved with it,
 * unchanged. A change here is a change to the manual.
 *
 * Each is one sentence. The bar shows its first word as the verb, in ink,
 * and the rest after it (clause()), as the web editor split them.
 */
#pragma once

#include "Model.h"

#include "Info.h"

#include <string>

namespace ni::tg::outcome
{

/* The file a panel proposes: "NI Trance Gate Slot 3.nitgslot" for slot 3,
 * "NI Trance Gate Bank.nitgbank" for all eight. */
std::string fileName (bool bank, int slot);
/* "nitgslot" or "nitgbank", and the open panel's filter for both. */
std::string extension (bool bank);
std::string openPatterns();

/* ---- the clipboard */
std::string copied (int slot);
std::string copyNotReady();
std::string copyNotWritten();
/* A paste's outcome: what the engine replaced, or why it refused. */
std::string pasted (const Transfer&, int slot);

/* ---- the files; `name` is the file's name, as the panel showed it */
std::string exported (bool bank, int slot, const std::string& name);
std::string exportNotReady();
std::string notWritten (const std::string& name);
std::string imported (const Transfer&, int slot, const std::string& name);
std::string notOpened (const std::string& name);
/* A file that cannot be one: a NUL in it. */
std::string notASlotFile (const std::string& name);
std::string noSavePanel();
std::string noOpenPanel();

/* An outcome as the bar shows it: the first word, then the rest. */
ni::ui::Clause clause (const std::string& sentence);

} // namespace ni::tg::outcome
