// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A key as the plugin window delivers it, and whether the window used it --
 * which is what decides whether the host hears it.
 *
 * JUCE'S PEER (ComponentPeer::handleKeyUpOrDown and handleKeyPress) gives a
 * key to the component with the keyboard focus, or to the window when nothing
 * has it, and then to each of its parents in turn: keyStateChanged as the key
 * goes down, keyPressed, keyStateChanged as it comes up. It stops at the first that uses it. A key none of them uses
 * the peer reports unused, and on macOS the plugin's view passes the NSEvent
 * up the responder chain to the view the host gave it -- that is how Space
 * reaches Live's transport from a plugin window. While a component is modal
 * the macOS peer uses every key itself (redirectKeyDown), so that counts as
 * used here too.
 *
 * The tests have no window (main.cpp), so no component can hold the keyboard
 * focus. windowUses walks the peer's chain itself, from the component that
 * stands for the focused one; the sweeps stand every component in for it in
 * turn, which covers wherever a click or Tab could leave the focus.
 *
 * A TEXT EDITOR IS THE EXCEPTION: it has the keyboard only while it is typed
 * into (TextField, a Readout's field, EditField), and there Space is a
 * character. The sweeps leave text editors and what is inside them out; the
 * tests that type hold them to taking Space.
 */
#pragma once

#include "Pointer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <typeinfo>
#include <vector>

namespace ni::ui::test
{

/* Space as the peer builds it: the key, and the character it types. */
inline const juce::KeyPress spaceKey { juce::KeyPress::spaceKey, {}, ' ' };
/* Live's Continue Play: the host's as much as Space alone. */
inline const juce::KeyPress shiftSpaceKey { juce::KeyPress::spaceKey, juce::ModifierKeys::shiftModifier, ' ' };

namespace detail
{
/* The peer's walk, from `focused` up through its parents to the first that
 * uses the key. JUCE also asks each one's KeyListeners, which only the peer
 * can reach; the kit and the editors register none. */
template <typename Use>
bool walk (juce::Component& focused, Use&& use)
{
    for (auto* target = &focused; target != nullptr; target = target->getParentComponent())
    {
        const juce::WeakReference<juce::Component> alive (target);
        if (use (*target))
            return true;
        if (alive == nullptr)
            return false;
    }
    return false;
}
} // namespace detail

/* Whether `focused` or a parent uses `key` -- down, pressed, up -- while
 * `focused` has the keyboard. */
inline bool chainUses (juce::Component& focused, const juce::KeyPress& key)
{
    const juce::Component::SafePointer<juce::Component> target (&focused);
    bool used = detail::walk (*target, [] (juce::Component& c) { return c.keyStateChanged (true); });
    if (target != nullptr)
        used = detail::walk (*target, [&key] (juce::Component& c) { return c.keyPressed (key); }) || used;
    if (target != nullptr)
        used = detail::walk (*target, [] (juce::Component& c) { return c.keyStateChanged (false); }) || used;
    return used;
}

/* Whether the window uses `key` while `focused` has the keyboard: the chain,
 * or a modal component. False: the peer hands it to the host. */
inline bool windowUses (juce::Component& focused, const juce::KeyPress& key)
{
    return chainUses (focused, key) || juce::Component::getCurrentlyModalComponent() != nullptr;
}

/* `root` and everything under it, parents before their children. */
inline std::vector<juce::Component*> everyComponent (juce::Component& root)
{
    std::vector<juce::Component*> out { &root };
    for (size_t i = 0; i < out.size(); ++i)
        for (auto* child : out[i]->getChildren())
            out.push_back (child);
    return out;
}

/* Whether `c` is a text editor or inside one. */
inline bool inTextEditor (const juce::Component& c)
{
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (dynamic_cast<const juce::TextEditor*> (p) != nullptr)
            return true;
    return false;
}

/* A component as a failure names it: its type, and its title or name. */
inline juce::String describe (const juce::Component& c)
{
    auto name = c.getTitle().isNotEmpty() ? c.getTitle() : c.getName();
    return juce::String (typeid (c).name()) + (name.isNotEmpty() ? " '" + name + "'" : juce::String());
}

/* Every component under `root`, text editors aside, that uses Space or
 * Shift+Space while it has the keyboard. Empty: the host hears both wherever
 * the keyboard is. The window's modal rule is left out: only a field being
 * typed into makes the window modal (a Readout's), and then the keys are
 * rightly the field's. */
inline juce::StringArray spaceUsers (juce::Component& root)
{
    juce::StringArray users;
    for (auto* c : everyComponent (root))
    {
        if (inTextEditor (*c))
            continue;
        const juce::Component::SafePointer<juce::Component> alive (c);
        for (const auto& k : { spaceKey, shiftSpaceKey })
            if (alive != nullptr && chainUses (*alive, k))
                users.addIfNotAlreadyThere (describe (*alive) + " (" + k.getTextDescription() + ")");
    }
    return users;
}

/* spaceUsers after a click on each control under `root` that takes the
 * keyboard, one after another: the states a click leaves -- pressed, on, a
 * list or a panel open -- included. */
inline juce::StringArray spaceUsersAfterClicks (juce::Component& root)
{
    std::vector<juce::Component::SafePointer<juce::Component>> controls;
    for (auto* c : everyComponent (root))
        if (c->getWantsKeyboardFocus() && ! inTextEditor (*c))
            controls.emplace_back (c);

    juce::StringArray users = spaceUsers (root);
    for (auto& control : controls)
    {
        if (control == nullptr)
            continue;
        const auto clicked = describe (*control);
        gallery::Pointer().click (*control, control->getLocalBounds().getCentre().toFloat());
        for (const auto& user : spaceUsers (root))
            users.addIfNotAlreadyThere (user + ", after a click on " + clicked);
    }
    return users;
}

} // namespace ni::ui::test
