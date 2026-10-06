// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * ni::WebPlugin -- the iPlug2 shell every Neon Ingvy plugin is built on.
 *
 * WHAT IT OWNS, so no product has to:
 *
 *   the WebView editor's bootstrap -- the string cap, a custom URL scheme (a
 *     WKWebView over file:// may refuse module scripts), developer tools in
 *     debug builds only, index.html, no rubber-band scroll;
 *   the editor protocol (ni/Editor.h) -- the ready handshake, the defaults,
 *     display strings, typed text and the window height;
 *   the animated ground's beat clock (ground.h), start to end;
 *   flush-to-zero around every block (shell_denormals.h);
 *   OnIdle's order: the ground first, then the product's host-facing work,
 *     then -- only while an editor is open -- its editor work.
 *
 * WHAT A PRODUCT SUPPLIES: ProcessAudio and ResetAudio, and whichever of the
 * idle and editor hooks it needs. Everything iPlug2 calls for the above is
 * `final` here; a product overrides SerializeState / UnserializeState and, if
 * it wants, OnParamChange and ProcessMidiMsg.
 *
 * THREADS. ProcessAudio runs on the audio thread; everything else here on the
 * main thread, except EditorIsOpen, which any thread may ask, and
 * OnParamChangeUI and OnRestoreState, which run wherever the host loads state
 * and only mark the editor stale off the main thread (editor::Stale). Engine
 * mutation goes through each engine's shell (docs/tech/architecture.md,
 * Threads).
 */
#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include "ni/Editor.h"
#include "ni/Wire.h"
#include "ground.h"
#include "shell_state.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#if !IPLUG_DSP || !defined(WEBVIEW_EDITOR_DELEGATE)
#error "ni::WebPlugin is a single-component plugin with a WebView editor"
#endif

namespace ni {

/* How the editor is found. */
struct WebEditor
{
  /* Its pages are served over this scheme rather than file://. One per
   * product: the scheme is also the page's origin. */
  const char* urlScheme;
  /* The product's own __FILE__: a debug build loads index.html beside it. */
  const char* sourceFile;
};

/* iplug::Plugin, spelled out: under CLAP an unqualified `Plugin` is
 * clap::helpers::Plugin. */
class WebPlugin : public iplug::Plugin, private editor::Port
{
public:
  WebPlugin(const iplug::InstanceInfo& info, const iplug::Config& config, const WebEditor& web);
  ~WebPlugin() override;

  void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames) final;
  void OnReset() final;
  void OnIdle() final;
  void OnUIOpen() final;
  void CloseWindow() final;
  void OnParamChangeUI(int paramIdx, iplug::EParamSource source) final;
  void OnRestoreState() final;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) final;

protected:
  /* ---- what a product supplies ---- */

  /* The audio thread, with denormals flushed and the ground already ticked. */
  virtual void ProcessAudio(iplug::sample** inputs, iplug::sample** outputs, int nFrames) = 0;
  /* The host's rate or block size changed. May not be the main thread. */
  virtual void ResetAudio() {}
  /* Every idle tick, editor or not: work the HOST depends on. */
  virtual void OnHostIdle() {}
  /* Every idle tick while an editor is open: work only the editor sees. */
  virtual void OnEditorIdle() {}
  /* The editor is listening (kReady, or open): send the product's own state.
   * The shell has just sent the defaults, the values and the display strings. */
  virtual void OnEditorReady() {}
  /* A message the shell does not handle. */
  virtual bool OnEditorMessage(int tag, const std::string& arg) { return false; }
  /* The EDITOR's text for a parameter, and what the editor's typed text means.
   * By default the parameter's own display and parser -- which is what the
   * host sees too. A product overrides these where the editor reads a value in
   * a unit the host's text cannot carry (the Trance Gate's stages in ms). */
  virtual void FormatDisplay(int paramIdx, WDL_String& str) const;
  virtual double ParseDisplay(int paramIdx, const char* text) const;

  /* ---- what a product may use ---- */

  /* Whether an editor is showing. Any thread; the audio thread gates its
   * capture on it. */
  bool EditorIsOpen() const { return mEditorOpen.load(std::memory_order_relaxed); }

  /* A payload whose buffer is sized at compile time, checked against the
   * transport's cap at compile time. */
  template <std::size_t N>
  void SendFramed(int tag, const char (&buf)[N], int len)
  {
    static_assert(wire::framed_size(int(N)) < editor::kMaxJSString,
                  "this buffer can outgrow the WebView's string cap");
    Send(tag, buf, len);
  }
  /* A payload sized at run time: refused, not truncated, when it would not
   * fit. */
  bool SendText(int tag, std::string_view text);
  /* A parameter's display string, under its own index. */
  void SendDisplay(int paramIdx);
  /* A value the plugin decided rather than the editor: through the host, so
   * automation and undo see it, and to the editor, which did not send it. */
  void SetParamFromPlugin(int paramIdx, double value);

  /* The editor's native view (an NSView* on macOS), or null while no editor
   * is open: what a system panel is shown on (ni/FileDialog.h). Main thread. */
  void* EditorView() const { return iplug::WebViewEditorDelegate::mView; }

  /* The host's clock, as the engines' transport structs hold it. The audio
   * thread. */
  wire::Transport HostTransport() const;

  /* The parameter block of a state chunk, for shell_state.h's loaders. */
  bool PutParams(iplug::IByteChunk& chunk) const { return SerializeParams(chunk); }
  int CheckParams(const iplug::IByteChunk& chunk, int pos) const
  {
    return shell::state::CheckParams(chunk, pos, *this);
  }
  int GetParams(const iplug::IByteChunk& chunk, int pos) { return UnserializeParams(chunk, pos); }

private:
  void Send(int tag, const char* data, int size);
  void SendGround();

  /* editor::Port */
  int PortParamCount() const override { return NParams(); }
  double PortDefault(int idx) const override;
  void PortSend(int tag, const char* data, int size) override { Send(tag, data, size); }
  void PortSendValues() override { SendCurrentParamValuesFromDelegate(); }
  void PortSendDisplay(int idx) override { SendDisplay(idx); }
  void PortSetFromText(int idx, const char* text) override;
  int PortHeight() const override { return GetEditorHeight(); }
  void PortResize(int height) override;
  void PortReady() override { OnEditorReady(); }
  void PortGroundRunning(bool running) override { mGroundRunning = running; }

  std::atomic<bool> mEditorOpen{false};
  /* Values and display strings a thread other than the main one changed, for
   * the next idle tick to send. */
  editor::Stale mStale;

  /*
   * THE GROUND'S BEAT CLOCK. Every window's ground rings on the host's beat,
   * and a WebView cannot see the host's transport -- so the audio thread keeps
   * time, and one message per ring crosses over (ground.h). mGroundFires is the
   * last count the editor was told, the main thread's alone.
   */
  gnd_t* mGround = nullptr;
  uint32_t mGroundFires = 0;
  /* The editor's ground is moving and wants a frame tick each idle tick
   * (kGroundRun, kGroundTick). The main thread's alone; false whenever there
   * is no editor. */
  bool mGroundRunning = false;
};

} // namespace ni
