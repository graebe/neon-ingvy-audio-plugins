// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Light that reaches past a component's bounds -- glow-led around a lit
 * button, glow-focus around a focused select -- painted where CSS paints it:
 * over the parent's background, under the component, and outside its box.
 *
 * A JUCE COMPONENT CANNOT PAINT OUTSIDE ITS BOUNDS: its paint() is clipped to
 * them. A box-shadow or a drop-shadow is not; a lit button's halo is most of
 * its look, and the web editors draw it 30px past the button. So the light of
 * a component is its PARENT's to paint:
 *
 *   the component    is Luminous: paintLight() draws its light (and only its
 *                    light) in its own coordinates, reaching at most
 *                    lightReach past its bounds; lightChanged(*this) when
 *                    the light changes (lit, unlit, focused, moved)
 *   the container    ends its paint() with paintChildLights(): every
 *                    Luminous child's light, over the container's own
 *                    background and under every child -- the order CSS
 *                    paints a box-shadow in
 *
 * Every container in the kit does the second, so a Luminous control placed in
 * any of them glows. A container of an editor's own that paints a background
 * and holds a Luminous control has to do it too; one that paints nothing can
 * leave it to its parent only if the light needs no background under it,
 * which in a window of panels it always does.
 *
 * What a component paints over ITSELF -- a lit step's glow inside its own
 * grid, the arc glow inside a knob's box -- it paints in its own paint(), as
 * it likes. This is only for light past the edge.
 */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::ui
{

/* How far past its bounds a component's light may reach: glow-led's 10px
 * deviation is gone by three of them. */
inline constexpr int lightReach = 32;

class Luminous
{
public:
    virtual ~Luminous() = default;

    /* This component's light, in its own coordinates. Called from its
     * parent's paint(), before any child is painted; draw nothing but light. */
    virtual void paintLight (juce::Graphics&) = 0;
};

/* The light of every visible direct child of `container` that is Luminous,
 * in child order: the last call of the container's paint(). */
void paintChildLights (juce::Graphics&, const juce::Component& container);

/* A Luminous component's light changed: repaints its parent where the light
 * falls now. A component that moves calls it before the move and after, so
 * the light it leaves behind is repainted too. Message thread. */
void lightChanged (juce::Component&);

} // namespace ni::ui
