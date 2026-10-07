// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Info in the hint bar. Info.h has the rules and the reasons.
 */
#include "Info.h"

namespace ni::ui
{

namespace
{
/* Where a component keeps its line. */
const juce::Identifier infoProperty { "ni.info" };

/* The dash a line is written with, between its name and the rest. */
const juce::String dash = juce::String::fromUTF8 (" \xe2\x80\x94 ");

void collect (const juce::Component& c, std::vector<juce::String>& out)
{
    const auto line = infoOf (c);
    if (line.isNotEmpty())
        out.push_back (line);
    for (auto* child : c.getChildren())
        collect (*child, out);
}
} // namespace

void infoStringIsLongerThan72Characters()
{
    /* Info.h: a line over the limit truncates in a narrow window and says
     * less than it should in every other. Shorten it. */
    jassertfalse;
}

/* ------------------------------------------------------------ tagging -- */

void setInfo (juce::Component& c, const juce::String& line)
{
    jassert (line.length() <= infoLimit);   // shorten the line (Info.h)

    if (line.isEmpty())
        c.getProperties().remove (infoProperty);
    else
        c.getProperties().set (infoProperty, line);

    c.setDescription (line);

    /* A line that changes under the pointer or the focus -- a control whose
     * meaning follows a mode -- is shown as it is now. */
    if (auto* host = InfoHost::find (c))
        host->infoState().refresh (&c, line);
}

juce::String infoOf (const juce::Component& c)
{
    return c.getProperties()[infoProperty].toString();
}

juce::Component* infoSource (juce::Component* c)
{
    for (; c != nullptr; c = c->getParentComponent())
        if (infoOf (*c).isNotEmpty())
            return c;
    return nullptr;
}

std::vector<juce::String> collectInfo (const juce::Component& root)
{
    std::vector<juce::String> out;
    collect (root, out);
    return out;
}

/* ------------------------------------------------------------- clauses -- */

Clause infoClause (const juce::String& line)
{
    const int at = line.indexOf (dash);
    if (at < 0)
        return { line, {} };
    /* The rest keeps its dash: "— what it does", drawn a space after the
     * name (Hint: the name in ink, the dash and the rest in ink-muted). */
    return { line.substring (0, at), line.substring (at + 1) };
}

HintContent hintClauses (const std::vector<Clause>& conventions,
                         const juce::String& info,
                         const std::optional<Clause>& status)
{
    HintContent out;
    if (status.has_value())
    {
        out.clauses.push_back (*status);
        for (size_t i = 0; i < conventions.size() && i < 2; ++i)
            out.clauses.push_back (conventions[i]);
        return out;
    }

    out.clauses = conventions;
    if (info.isNotEmpty())
        out.info = infoClause (info);
    return out;
}

/* --------------------------------------------------------------- state -- */

InfoState::InfoState (Clock c)
    : clock (c ? std::move (c) : Clock ([] { return juce::Time::getMillisecondCounterHiRes(); }))
{
}

InfoState::~InfoState()
{
    stopTimer();
}

bool InfoState::shows (const Source& s) const
{
    if (s.line.isEmpty())
        return false;
    return s.leftAt < 0.0 || clock() - s.leftAt < (double) infoGraceMs;
}

void InfoState::show (Source& s, const juce::String& line, juce::Component* from)
{
    s.line = line;
    s.from = from;
    s.leftAt = -1.0;
    changed();
}

void InfoState::hide (Source& s)
{
    if (s.line.isEmpty() || s.leftAt >= 0.0)
        return;
    s.leftAt = clock();
    /* The grace's end is only a repaint: text() already knows the time. */
    startTimer (infoGraceMs);
    changed();
}

void InfoState::enter (const juce::String& line, juce::Component* from)
{
    if (line.isEmpty())
        leave();
    else
        show (pointer, line, from);
}

void InfoState::leave() { hide (pointer); }

void InfoState::focus (const juce::String& line, juce::Component* from)
{
    if (line.isEmpty())
        blur();
    else
        show (keyboard, line, from);
}

void InfoState::blur() { hide (keyboard); }

void InfoState::refresh (juce::Component* from, const juce::String& line)
{
    for (auto* s : { &pointer, &keyboard })
    {
        if (from == nullptr || s->from.getComponent() != from || s->line.isEmpty())
            continue;
        if (line.isEmpty())
            hide (*s);
        else
        {
            s->line = line;
            changed();
        }
    }
}

juce::String InfoState::text() const
{
    if (shows (pointer))
        return pointer.line;
    if (shows (keyboard))
        return keyboard.line;
    return {};
}

void InfoState::poll()
{
    for (auto* s : { &pointer, &keyboard })
    {
        if (s->leftAt >= 0.0 && ! shows (*s))
        {
            s->line.clear();
            s->from = nullptr;
            s->leftAt = -1.0;
        }
    }

    const bool pending = (pointer.leftAt >= 0.0) || (keyboard.leftAt >= 0.0);
    if (! pending)
        stopTimer();

    changed();
}

void InfoState::changed()
{
    /* Only a change of what is shown is news; a grace beginning is not. */
    const auto now = text();
    if (now == lastText)
        return;
    lastText = now;
    if (onChange)
        onChange();
}

void InfoState::timerCallback()
{
    poll();
}

/* ---------------------------------------------------------------- host -- */

InfoHost* InfoHost::find (juce::Component& c)
{
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (auto* host = dynamic_cast<InfoHost*> (p))
            return host;
    return nullptr;
}

/* ------------------------------------------------------------- tracker -- */

InfoTracker::InfoTracker (juce::Component& r, InfoState& s)
    : root (r), state (s)
{
    root.addMouseListener (this, true);
}

InfoTracker::~InfoTracker()
{
    root.removeMouseListener (this);
}

void InfoTracker::point (juce::Component* under)
{
    if (under != nullptr && under != &root && ! root.isParentOf (under))
        under = nullptr;

    if (auto* source = infoSource (under))
        state.enter (infoOf (*source), source);
    else
        state.leave();
}

void InfoTracker::mouseEnter (const juce::MouseEvent& e)
{
    /* While a button is held the pointer is dragging something, and the bar
     * keeps saying what. */
    if (! e.mods.isAnyMouseButtonDown())
        point (e.eventComponent);
}

void InfoTracker::mouseExit (const juce::MouseEvent& e)
{
    if (e.mods.isAnyMouseButtonDown())
        return;

    /* Into another component of the window, its enter follows and decides;
     * out of the window, nothing follows, so leave now. */
    const auto at = e.getScreenPosition();
    const auto* under = juce::Desktop::getInstance().findComponentAt (at);
    if (under == nullptr || (under != &root && ! root.isParentOf (under)))
        state.leave();
}

void InfoTracker::mouseUp (const juce::MouseEvent& e)
{
    /* The drag is over: catch up with what the pointer is over now. */
    point (juce::Desktop::getInstance().findComponentAt (e.getScreenPosition()));
}

} // namespace ni::ui
