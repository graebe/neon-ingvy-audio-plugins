/*
 * ni::editor -- the part of the editor protocol every plugin speaks the same
 * way, with no host in it.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * The editor is a WebView. It holds normalised parameter values and nothing
 * else, so the plugin formats every readout, parses every typed value and
 * sizes the window; and a push from OnUIOpen lands before the editor's module
 * script has run, so the editor asks for its state (kReady) once it is
 * listening. ni::WebPlugin implements Port with iPlug2's calls;
 * tests/cpp/ni_editor.cpp implements it with a recorder.
 *
 * TAGS 0..NParams()-1 carry a parameter's display string, tagged with its
 * index. Each product's own tags are 64..111. The shell's are below, the same
 * in every plugin, and mirrored in ui-kit/src/lib/shell.js
 * (tests/editor_tags.test.mjs holds the two together).
 */
#pragma once

#include <string>
#include <string_view>

namespace ni {
namespace editor {

enum Tag : int
{
  /* -> editor: one ring, "<strength>" in 0..1 with three decimals. Sent only
   * when the ground's beat clock rings: one message is one ring. */
  kGround = 112,
  /* -> editor: every parameter's DEFAULT, normalised, "<d0>:<d1>:...:<dN-1>"
   * in parameter-index order -- what a reset sets. Sent with the state on
   * kReady and on open; an empty payload for a plugin with no parameters. */
  kDefaults = 113,
  /* -> editor: the ground's frame clock -- an empty message on every idle
   * tick (~50 Hz) while the editor reports its ground moving (kGroundRun).
   * A host's WebKit shows the editor as a hidden page and throttles its
   * timers to a few hertz, but delivers these at once, so the field steps on
   * them (ui-kit/src/lib/field.js). */
  kGroundTick = 114,

  /* <- editor: mounted and listening -- send the whole state. */
  kReady = 120,
  /* <- editor: "<paramIdx>:<typed text>", parsed by the parameter itself and
   * sent through the host like any other edit. */
  kSetText = 121,
  /* <- editor: the height it needs in viewport pixels, a whole number. */
  kHeight = 122,
  /* <- editor: "1" while its ground's field is moving -- a ring in flight,
   * Motion on -- and "0" once it is at rest or switched off. */
  kGroundRun = 123,
};

/* The transport's cap on one message, raised from iPlug2's 8192. It
 * TRUNCATES rather than fails, so every payload is sized under it. */
constexpr int kMaxJSString = 65536;

/* What the protocol needs from a plugin. */
class Port
{
public:
  virtual int PortParamCount() const = 0;
  virtual double PortDefault(int idx) const = 0;          /* normalised */
  virtual void PortSend(int tag, const char* data, int size) = 0;
  virtual void PortSendValues() = 0;                       /* every value, normalised */
  virtual void PortSendDisplay(int idx) = 0;
  virtual void PortSetFromText(int idx, const char* text) = 0;
  virtual int PortHeight() const = 0;
  virtual void PortResize(int height) = 0;
  /* The product's own state, after the shell's. */
  virtual void PortReady() = 0;
  /* Whether the editor's ground is moving, so wants kGroundTick. */
  virtual void PortGroundRunning(bool running) = 0;

protected:
  ~Port() = default;
};

/* kDefaults' payload: up to nine decimals, trailing zeros dropped, '.' always. */
std::string EncodeDefaults(const Port& port);

/* kGround's payload. */
std::string EncodeGround(float strength);

/* Everything an editor that has just started listening needs: the defaults,
 * every value, every display string, then the product's own. */
void SendAll(Port& port);

/* A shell message from the editor. False for a tag that is not the shell's. */
bool Handle(Port& port, int tag, std::string_view arg);

} // namespace editor
} // namespace ni
