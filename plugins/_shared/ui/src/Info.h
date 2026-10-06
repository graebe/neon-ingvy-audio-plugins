// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Info: what the control under the pointer or the keyboard does, in the hint
 * bar -- Ultraviolet 1.1.0's Hint, and the web kit's lib/info.js, natively.
 *
 * A CONTROL DECLARES ITS LINE ONCE, with setInfo(): "Name — what it does", at
 * most 72 characters, written in the editor that owns the control (each
 * editor keeps its lines together in one place, as the web editors' info.js
 * files do). The line is also the control's accessible description, so a
 * screen reader hears what a sighted user reads. The window does the rest:
 *
 *   InfoTracker   one mouse listener on the window's root that hears every
 *                 component in it and finds the nearest line above whatever
 *                 the pointer is over -- a control added later needs nothing
 *                 but its line
 *   FocusVisibility (Focus.h) reports a control's VISIBLE keyboard focus
 *   InfoState     the two sources, pointer and focus, and their timing
 *   hintClauses() what the bar shows, by the system's precedence
 *
 * WHAT THE BAR SHOWS, IN ORDER (Hint README, "Precedence"):
 *
 *   1. an action's outcome ("Copied slot 1."), in the first clause's place for
 *      the seconds it is shown -- the user just asked for it;
 *   2. the line under the pointer, else the line on the visible keyboard
 *      focus -- the pointer is the more recent intent, and when it leaves, a
 *      focused control's line is still true;
 *   3. the window's conventions.
 *
 * IN AT ONCE, OUT AFTER A BEAT: a line replaces the bar the moment the pointer
 * arrives; on leaving, the bar waits 150 ms before it goes back, and an
 * arrival inside that wait cancels it -- so a pointer moving along a row of
 * knobs, across the gaps between them, goes from line to line and never
 * flashes the conventions in between. While a button is held the bar keeps
 * the line of what is being dragged, and catches up on release.
 *
 * TIMERS ONLY REDRAW. What the bar says is a function of the events and the
 * time now (InfoState::text()); the timer that runs out a grace only asks for
 * a repaint. A bar that is never repainted is out of date, never wrong.
 *
 * Message thread throughout.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

namespace ni::ui
{

/* "Every control declares what it does in one line of at most 72 characters." */
inline constexpr int infoLimit = 72;

/* "It arrives at once and leaves after a 150 ms grace." */
inline constexpr int infoGraceMs = 150;

/* The characters of UTF-8 text, as the limit counts them: code points, so
 * "—" is one, not the three bytes it takes. */
constexpr int utf8Length (const char* text, std::size_t bytes)
{
    int n = 0;
    for (std::size_t i = 0; i < bytes; ++i)
        if ((static_cast<unsigned char> (text[i]) & 0xc0u) != 0x80u)
            ++n;
    return n;
}

/* Called only for a line over the limit -- and not constexpr, so a constant
 * InfoText that reaches it fails to compile, with this name in the error. */
void infoStringIsLongerThan72Characters();

/*
 * A line written in the source, checked against the limit where it is
 * written:
 *
 *   inline constexpr ni::ui::InfoText rate { "Rate — the length of one step, synced to the song tempo." };
 *
 * A constexpr one over 72 characters does not compile. Built at run time --
 * from a non-constant -- it asserts instead. Sources are UTF-8 (the dash
 * is), and a Windows build must read them so (clang-cl does; MSVC wants
 * /utf-8).
 */
class InfoText
{
public:
    template <std::size_t N>
    constexpr InfoText (const char (&utf8)[N]) : text (utf8), bytes (N - 1)
    {
        if (utf8Length (utf8, N - 1) > infoLimit)
            infoStringIsLongerThan72Characters();
    }

    juce::String str() const { return juce::String::fromUTF8 (text, (int) bytes); }
    operator juce::String() const { return str(); }

    constexpr const char* utf8() const noexcept { return text; }
    constexpr int length() const noexcept { return utf8Length (text, bytes); }

private:
    const char* text;
    std::size_t bytes;
};

/* ------------------------------------------------------------ tagging -- */

/* Gives `c` its line, and its accessible description. Over the limit is a
 * programming error, asserted -- and the editors' tests walk every line
 * (collectInfo) to keep it so. An empty string removes it. */
void setInfo (juce::Component& c, const juce::String& line);

/* `c`'s own line, or empty. */
juce::String infoOf (const juce::Component& c);

/* The component whose line applies at `c`: `c` itself or its nearest
 * ancestor with one -- the web kit's closest('[data-info]'). nullptr when
 * none does. */
juce::Component* infoSource (juce::Component* c);

/* Every line in the tree under `root`, `root`'s included, in tree order: what
 * a test holds to the limit. */
std::vector<juce::String> collectInfo (const juce::Component& root);

/* ------------------------------------------------------------- clauses -- */

/* One clause of the bar: the name in `ink`, the rest in `ink-muted`. */
struct Clause
{
    juce::String name;
    juce::String rest;

    bool operator== (const Clause& o) const { return name == o.name && rest == o.rest; }
};

/* A line as a clause, split at its " — ": { "Rate", "— the length of one
 * step, …" }. With no dash, it is all name. */
Clause infoClause (const juce::String& line);

/* What the bar shows: `clauses` -- the conventions, with an outcome in first
 * place while there is one (and then two conventions after it, three at
 * most) -- and `info`, the clause laid over them, or none. The conventions
 * stay underneath an info line, hidden, so the bar never changes width. */
struct HintContent
{
    std::vector<Clause> clauses;
    std::optional<Clause> info;
};

HintContent hintClauses (const std::vector<Clause>& conventions,
                         const juce::String& info,
                         const std::optional<Clause>& status = std::nullopt);

/* --------------------------------------------------------------- state -- */

class InfoState final : private juce::Timer
{
public:
    /* Milliseconds, monotonic. Injected by a test; the real one otherwise. */
    using Clock = std::function<double()>;

    explicit InfoState (Clock clock = {});
    ~InfoState() override;

    /* The pointer arrived over a line, or left it. */
    void enter (const juce::String& line, juce::Component* from = nullptr);
    void leave();

    /* A control gained visible keyboard focus, or lost it. */
    void focus (const juce::String& line, juce::Component* from = nullptr);
    void blur();

    /* `from`'s line changed under a source that is on it: show the new one. */
    void refresh (juce::Component* from, const juce::String& line);

    /* The line the bar shows now -- the pointer's, else the focus's -- or
     * empty for the conventions. */
    juce::String text() const;

    /* Called whenever text() may have changed: at once for an event, and
     * when a grace runs out. The bar repaints. */
    std::function<void()> onChange;

    /* Ends any grace that has run out by the clock: what the timer does, and
     * what a test does after moving its clock. */
    void poll();

private:
    struct Source
    {
        juce::String line;
        juce::Component::SafePointer<juce::Component> from;
        double leftAt = -1.0;   // when the grace began; -1 while shown
    };

    bool shows (const Source&) const;
    void show (Source&, const juce::String& line, juce::Component* from);
    void hide (Source&);
    void changed();
    void timerCallback() override;

    Clock clock;
    Source pointer, keyboard;
    juce::String lastText;
};

/* ---------------------------------------------------------------- host -- */

/*
 * The window that owns the bar's state: an editor's frame implements this,
 * and a control finds it above itself to report its focus.
 */
class InfoHost
{
public:
    virtual ~InfoHost() = default;
    virtual InfoState& infoState() = 0;

    /* `c` itself or the nearest ancestor that is a host, or nullptr. */
    static InfoHost* find (juce::Component& c);
};

/*
 * The pointer's half, delegated: listens to every component inside `root`
 * and tells `state` what the pointer is over. Lives as long as the root.
 */
class InfoTracker final : private juce::MouseListener
{
public:
    InfoTracker (juce::Component& root, InfoState& state);
    ~InfoTracker() override;

private:
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void point (juce::Component* under);

    juce::Component& root;
    InfoState& state;

    JUCE_DECLARE_NON_COPYABLE (InfoTracker)
};

} // namespace ni::ui
