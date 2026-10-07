// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Trance Gate's window verbs. Verbs.h has what each one does.
 */
#include "Verbs.h"

#include "InfoLines.h"
#include "Outcome.h"

#include <iterator>

namespace ni::tg
{

namespace
{
/* The icon, the full verb and its line, in the card's order. */
struct Face
{
    const char* icon;
    const char* title;
    const ni::ui::InfoText& line;
};

const Face faces[] {
    { "copy", "Copy slot", info::copy },
    { "paste", "Paste slot", info::paste },
    { "export", "Export slot", info::exportSlot },
    { "export-all", "Export all", info::exportAll },
    { "import", "Import", info::import },
    { "shuffle", "Randomize", info::randomize },
};
static_assert (std::size (faces) == (std::size_t) Verbs::Verb::count);

/* The text of a file, as the engine reads it: up to the largest it takes,
 * and a byte more so an oversized one is refused in the engine's words. */
constexpr std::size_t readLimit = (std::size_t) maxFileBytes + 1;

/* A file's name as the panel showed it, for the hint bar. */
std::string nameOf (const juce::File& file)
{
    return file.getFileName().toStdString();
}
} // namespace

Verbs::Verbs (Model& m, Clipboard& c, FilePanels& f, Report r)
    : model (m), clipboard (c), files (f), reportTo (std::move (r))
{
    for (std::size_t i = 0; i < buttons.size(); ++i)
    {
        auto b = std::make_unique<ni::ui::Button> (juce::String(), faces[i].icon);
        b->setTitle (faces[i].title);
        ni::ui::setInfo (*b, faces[i].line);
        icons.add (*b);
        buttons[i] = std::move (b);
    }
    icons.setTitle ("Slot actions");

    button (Verb::copy).onClick = [this] { copy(); };
    button (Verb::paste).onClick = [this] { paste(); };
    button (Verb::exportSlot).onClick = [this] { exportFile (false); };
    button (Verb::exportAll).onClick = [this] { exportFile (true); };
    button (Verb::import).onClick = [this] { import(); };
    button (Verb::randomize).onClick = [this] { randomize(); };

    icons.setSize (icons.idealSize().getWidth(), icons.idealSize().getHeight());
}

Verbs::~Verbs() = default;

int Verbs::slot() const
{
    auto& p = model.parameter (param::slot);
    return juce::roundToInt (p.convertFrom0to1 (p.getValue()));
}

void Verbs::report (const std::string& sentence) const
{
    if (reportTo)
        reportTo (sentence);
}

void Verbs::copy()
{
    const int n = slot();
    const auto text = model.exportText (false);
    if (text.empty())
        return report (outcome::copyNotReady());
    if (! clipboard.write (text))
        return report (outcome::copyNotWritten());
    report (outcome::copied (n));
}

void Verbs::paste()
{
    auto text = clipboard.read().value_or (std::string());
    if (text.size() > readLimit)
        text.resize (readLimit);
    const auto result = model.paste (text);
    report (outcome::pasted (result, slot()));
}

void Verbs::exportFile (bool bank)
{
    /* The slot the press was on names the file; the text is taken when the
     * panel answers. */
    const int n = slot();
    const auto name = outcome::fileName (bank, n);
    juce::WeakReference<Verbs> self (this);
    const bool shown = files.save (model.lastFolder(), juce::String::fromUTF8 (name.c_str()),
                                   "*." + juce::String (outcome::extension (bank)),
                                   [self, bank, n] (const juce::File& file)
    {
        if (self == nullptr || file == juce::File())
            return;
        auto& v = *self;
        v.model.setLastFolder (file.getParentDirectory());
        const auto text = v.model.exportText (bank);
        if (text.empty())
            return v.report (outcome::exportNotReady());
        if (! v.files.write (file, text))
            return v.report (outcome::notWritten (nameOf (file)));
        v.report (outcome::exported (bank, n, nameOf (file)));
    });
    if (! shown)
        report (outcome::noSavePanel());
}

void Verbs::import()
{
    juce::WeakReference<Verbs> self (this);
    const bool shown = files.open (model.lastFolder(), outcome::openPatterns(), [self] (const juce::File& file)
    {
        if (self == nullptr || file == juce::File())
            return;
        auto& v = *self;
        v.model.setLastFolder (file.getParentDirectory());
        const auto name = nameOf (file);
        const auto text = v.files.read (file, readLimit);
        if (! text)
            return v.report (outcome::notOpened (name));
        if (text->find ('\0') != std::string::npos)
            return v.report (outcome::notASlotFile (name));
        /* The slot the file lands in is the one current when it arrives. */
        const auto result = v.model.importText (*text);
        v.report (outcome::imported (result, v.slot(), name));
    });
    if (! shown)
        report (outcome::noOpenPanel());
}

void Verbs::randomize()
{
    model.randomize();
}

} // namespace ni::tg
