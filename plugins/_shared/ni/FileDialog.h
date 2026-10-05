/*
 * ni::FileDialog -- the system's save and open panels, for a WebView editor.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * WHY THE PLUGIN AND NOT THE PAGE. A WKWebView inside a plugin has no download
 * manager, so a page cannot save a file at all; iPlug2's UI delegate does run an
 * open panel for <input type=file>, but only for opening, with no say over the
 * folder or the types. So both directions are the plugin's: the editor asks with
 * a message, this shows the panel as a sheet on the editor's window, and the
 * plugin answers the editor with the outcome. The page only ever sees text.
 *
 * THE LAST FOLDER IS REMEMBERED, per product, across sessions: in the user's
 * preferences under the domain the plugin names (its bundle id), shared by its
 * save and open panels -- where you exported is where you will look to import.
 *
 * THREADS. The main thread only: call from an editor message, and `done` runs
 * on the main thread when the panel closes -- never after this object is gone,
 * and never twice. One panel at a time; asking while one is up is refused.
 *
 * macOS only (AppKit): cmake/NiPlugin.cmake compiles FileDialog.mm on Apple,
 * which is every platform these plugins ship on.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ni {

class FileDialog
{
public:
  /* The chosen file's path; empty when the panel was cancelled. */
  using Done = std::function<void(const std::string& path)>;

  /* `prefsDomain`: where the last folder is remembered, e.g. the bundle id. */
  explicit FileDialog(std::string prefsDomain);
  ~FileDialog();
  FileDialog(const FileDialog&) = delete;
  FileDialog& operator=(const FileDialog&) = delete;

  /* A save panel on `view`'s window (an NSView*), proposing `name`, for a file
   * of extension `ext` (no dot). False when it could not be shown. */
  bool Save(void* view, const std::string& name, const std::string& ext, Done done);
  /* An open panel on `view`'s window, for any of `exts`. */
  bool Open(void* view, const std::vector<std::string>& exts, Done done);

  /* The platform's state, opaque here. */
  struct Impl;

private:
  std::shared_ptr<Impl> mImpl;
};

} // namespace ni
