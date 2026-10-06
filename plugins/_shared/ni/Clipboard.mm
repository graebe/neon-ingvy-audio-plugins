// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * ni::Clipboard on macOS: the general pasteboard, as plain text. See
 * Clipboard.h.
 */
#include "ni/Clipboard.h"

#import <AppKit/AppKit.h>

namespace ni {

namespace {

class SystemClipboard final : public Clipboard
{
public:
  bool Read(std::string& text) override
  {
    NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (!s)
      return false;
    const char* utf8 = [s UTF8String];
    if (!utf8)
      return false;
    text.assign(utf8);
    return true;
  }

  bool Write(const std::string& text) override
  {
    NSString* s = [[NSString alloc] initWithBytes:text.data()
                                           length:text.size()
                                         encoding:NSUTF8StringEncoding];
    if (!s)
      return false;
    NSPasteboard* board = [NSPasteboard generalPasteboard];
    [board clearContents];
    return [board setString:s forType:NSPasteboardTypeString];
  }
};

} // namespace

Clipboard& Clipboard::System()
{
  static SystemClipboard system;
  return system;
}

} // namespace ni
