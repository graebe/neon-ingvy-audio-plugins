// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The host window's contents. Window.h says what they are.
 */
#include "Window.h"

namespace ni::tg
{

Window::Window (Model& model) : gate (model, clipboard, panels)
{
    setTitle ("NI Trance Gate");
    addAndMakeVisible (gate);
    setSize (gate.getWidth(), gate.getHeight());
}

Window::~Window() = default;

void Window::childBoundsChanged (juce::Component* child)
{
    if (child == &gate && (gate.getWidth() != getWidth() || gate.getHeight() != getHeight()))
        setSize (gate.getWidth(), gate.getHeight());
}

} // namespace ni::tg
