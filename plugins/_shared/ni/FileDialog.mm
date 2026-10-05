/*
 * ni::FileDialog on macOS: NSSavePanel and NSOpenPanel, as sheets. See
 * FileDialog.h.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "ni/FileDialog.h"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace ni {

static NSString* const kLastFolderKey = @"NILastFolder";

struct FileDialog::Impl
{
  std::string domain;
  /* The panel on screen, if any: retained while it is up. */
  NSSavePanel* panel = nil;
  NSWindow* window = nil;

  NSUserDefaults* Prefs() const
  {
    return domain.empty() ? nil
                          : [[NSUserDefaults alloc] initWithSuiteName:[NSString stringWithUTF8String:domain.c_str()]];
  }

  NSURL* LastFolder() const
  {
    NSString* path = [Prefs() stringForKey:kLastFolderKey];
    BOOL dir = NO;
    if (path && [[NSFileManager defaultManager] fileExistsAtPath:path isDirectory:&dir] && dir)
      return [NSURL fileURLWithPath:path isDirectory:YES];
    return nil;
  }

  void Remember(NSURL* file) const
  {
    NSString* folder = [[file URLByDeletingLastPathComponent] path];
    if (folder)
      [Prefs() setObject:folder forKey:kLastFolderKey];
  }
};

/*
 * Shows `panel` as a sheet on `view`'s window and calls `done` once with the
 * chosen URL or nil. The completion holds the Impl only weakly: a plugin
 * destroyed while the sheet is up is not called back.
 */
static bool Present(const std::shared_ptr<FileDialog::Impl>& impl, NSSavePanel* panel, void* view,
                    FileDialog::Done done)
{
  NSWindow* window = [(__bridge NSView*) view window];
  if (!window || impl->panel)
    return false;
  if (NSURL* folder = impl->LastFolder())
    panel.directoryURL = folder;
  impl->panel = panel;
  impl->window = window;
  std::weak_ptr<FileDialog::Impl> weak = impl;
  [panel beginSheetModalForWindow:window
                completionHandler:^(NSModalResponse result) {
                  auto self = weak.lock();
                  if (!self)
                    return;
                  NSURL* url = (result == NSModalResponseOK) ? panel.URL : nil;
                  self->panel = nil;
                  self->window = nil;
                  if (url)
                    self->Remember(url);
                  done(url ? std::string(url.fileSystemRepresentation) : std::string());
                }];
  return true;
}

static NSArray<UTType*>* TypesFor(const std::vector<std::string>& exts)
{
  NSMutableArray<UTType*>* types = [NSMutableArray array];
  for (const std::string& ext : exts)
  {
    UTType* t = [UTType typeWithFilenameExtension:[NSString stringWithUTF8String:ext.c_str()]
                                 conformingToType:UTTypeData];
    if (t)
      [types addObject:t];
  }
  return types;
}

FileDialog::FileDialog(std::string prefsDomain)
: mImpl(std::make_shared<Impl>())
{
  mImpl->domain = std::move(prefsDomain);
}

/* A sheet still up is ended, and -- the Impl being gone -- calls no one. */
FileDialog::~FileDialog()
{
  NSSavePanel* panel = mImpl->panel;
  NSWindow* window = mImpl->window;
  mImpl.reset();
  if (panel && window)
    [window endSheet:panel returnCode:NSModalResponseCancel];
}

bool FileDialog::Save(void* view, const std::string& name, const std::string& ext, Done done)
{
  if (!view || !done)
    return false;
  NSSavePanel* panel = [NSSavePanel savePanel];
  panel.allowedContentTypes = TypesFor({ext});
  panel.nameFieldStringValue = [NSString stringWithUTF8String:name.c_str()];
  panel.canCreateDirectories = YES;
  return Present(mImpl, panel, view, std::move(done));
}

bool FileDialog::Open(void* view, const std::vector<std::string>& exts, Done done)
{
  if (!view || !done)
    return false;
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.allowedContentTypes = TypesFor(exts);
  panel.canChooseFiles = YES;
  panel.canChooseDirectories = NO;
  panel.allowsMultipleSelection = NO;
  return Present(mImpl, panel, view, std::move(done));
}

} // namespace ni
