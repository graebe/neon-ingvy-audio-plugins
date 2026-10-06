// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * ni::Clipboard -- the system's clipboard as text, for a WebView editor.
 *
 * WHY THE PLUGIN AND NOT THE PAGE. Inside a plugin the page cannot reach the
 * clipboard: a WKWebView grants navigator.clipboard.readText no permission it
 * could ask for, and the paste shortcut never arrives, because the host's menu
 * takes ⌘V before the plugin's window sees it. So copy and paste are messages:
 * the editor asks, the plugin reads or writes the clipboard here, and answers
 * with what happened. The page only ever sees the outcome.
 *
 * AN INTERFACE, so a test can stand a fake in for the system's: the code that
 * moves text between the engine and the clipboard takes a Clipboard&, and the
 * plugin hands it System().
 *
 * THREADS. The main thread only: call from an editor message.
 *
 * macOS only (AppKit's NSPasteboard): cmake/NiPlugin.cmake compiles
 * Clipboard.mm on Apple, which is every platform these plugins ship on.
 */
#pragma once

#include <string>

namespace ni {

class Clipboard
{
public:
  virtual ~Clipboard() = default;

  /* The clipboard's text into `text`; false when it holds none. */
  virtual bool Read(std::string& text) = 0;
  /* Replaces the clipboard's contents with `text`; false when it could not. */
  virtual bool Write(const std::string& text) = 0;

  /* The system's general clipboard. */
  static Clipboard& System();
};

} // namespace ni
