// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Light that has to pass through a container on its way out: the half of
 * Luminous.h a nested control needs.
 *
 * Luminous.h has a control's light painted by its parent. The kit's controls
 * are often a level deeper than that -- a knob's dial inside its card, a
 * button inside a joined group, a switch inside a CheckList row -- and a
 * dial's focus ring reaches past the card as well as past the dial. So every
 * control of the kit keeps ONE rule about where its light is painted:
 *
 *   inside its own bounds    in its own paint(), in the order CSS paints it
 *                            (a box-shadow under the box, a lit square's
 *                            halo over its housing)
 *   outside its own bounds   in paintLight(), and nowhere else
 *
 * so no pixel of light is painted twice. A container keeps the same rule for
 * its children: it paints their light inside itself (paintChildLights() at the
 * end of its paint(), over its own background), and if that light can reach
 * past the container too, the container is itself Luminous and passes the
 * rest up with forwardChildLights().
 */
#pragma once

#include "Luminous.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ni::ui
{

/* The part of the light of `container`'s children that falls outside
 * `container`: what a container's paintLight() passes up to its parent. */
void forwardChildLights (juce::Graphics&, const juce::Component& container);

/* One child's light, over whatever `g` already holds -- for a child raised
 * above its siblings, whose light CSS paints over them (a hovered button in a
 * joined group). `g` is in `container`'s coordinates. */
void paintChildLight (juce::Graphics&, const juce::Component& container, juce::Component& child);

/* Clips `g` to everything outside `c`'s own bounds: the first line of a
 * paintLight() that keeps the rule above. */
void excludeOwnBounds (juce::Graphics&, const juce::Component& c);

/*
 * `c`'s light changed: repaints where it falls, in every ancestor it can
 * reach -- lightChanged() repaints the parent only, and a light forwarded
 * through a container falls in the grandparent too. Message thread.
 */
void relight (juce::Component& c);

} // namespace ni::ui
