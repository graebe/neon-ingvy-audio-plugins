/*
 * ni::WebPlugin. See WebPlugin.h.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 */
#include "ni/WebPlugin.h"
#include "shell_denormals.h"

namespace ni {

using namespace iplug;

WebPlugin::WebPlugin(const InstanceInfo& info, const Config& config, const WebEditor& web)
: iplug::Plugin(info, config)
{
  SetMaxJSStringLength(editor::kMaxJSString);
  SetCustomUrlScheme(web.urlScheme);
#ifndef NDEBUG
  SetEnableDevTools(true);
#endif
  /* Here and not in OnReset, which may run on a real-time thread and must not
   * allocate; OnReset corrects the rate. */
  mGround = gnd_new(GetSampleRate());

  /* Without LoadIndexHtml a WKWebView is a plain white rectangle, silently. */
  const std::string source = web.sourceFile ? web.sourceFile : "";
  mEditorInitFunc = [this, source]() {
    LoadIndexHtml(source.c_str(), GetBundleID());
    EnableScroll(false);
  };
}

WebPlugin::~WebPlugin()
{
  gnd_free(mGround);
  mGround = nullptr;
}

void WebPlugin::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  /* No denormals for the block, the engines included; the host's mode comes
   * back on the way out. */
  const shell::ScopedFlushDenormals ftz;

  /* The ground keeps the host's time, once a block, from the transport the
   * wrapper has just read: a ring a beat while it plays (ground.h). It reads
   * no audio, so every plugin rings alike. The values go through as the host
   * gave them; what a missing tempo or meter means is decided in ground-core. */
  int num = 0, den = 0;
  GetTimeSig(num, den);
  gnd_tick(mGround, GetPPQPos(), GetTempo(), num, den, GetTransportIsRunning() ? 1 : 0, nFrames);

  ProcessAudio(inputs, outputs, nFrames);
}

void WebPlugin::OnReset()
{
  /* Requests the clock applies at its next block, whatever thread this is.
   * Neither touches the ring count the editor compares against. */
  gnd_set_sample_rate(mGround, GetSampleRate());
  gnd_reset(mGround);
  ResetAudio();
}

/* The ground FIRST, so nothing a product does can starve it. OnIdle runs off
 * the API wrapper's timer, window or not. */
void WebPlugin::OnIdle()
{
  SendGround();
  OnHostIdle();
  if (EditorIsOpen())
    OnEditorIdle();
}

void WebPlugin::OnUIOpen()
{
  iplug::Plugin::OnUIOpen();
  mEditorOpen.store(true, std::memory_order_relaxed);
  gnd_set_active(mGround, 1);
  /* Usually too early to be heard -- the module script has not run -- which
   * is what kReady is for. It covers a page that is already live. */
  editor::SendAll(*this);
}

/* Here and not in OnUIClose, which WebViewEditorDelegate::CloseWindow never
 * calls. */
void WebPlugin::CloseWindow()
{
  mEditorOpen.store(false, std::memory_order_relaxed);
  gnd_set_active(mGround, 0);
  iplug::Plugin::CloseWindow();
}

void WebPlugin::OnParamChangeUI(int paramIdx, EParamSource)
{
  SendDisplay(paramIdx);
}

bool WebPlugin::OnMessage(int msgTag, int, int dataSize, const void* pData)
{
  /* pData is dataSize bytes out of a base64 decode, not a C string. */
  const std::string arg = (pData && dataSize > 0)
                            ? std::string(static_cast<const char*>(pData), size_t(dataSize))
                            : std::string();
  return editor::Handle(*this, msgTag, arg) || OnEditorMessage(msgTag, arg);
}

bool WebPlugin::SendText(int tag, std::string_view text)
{
  if (wire::framed_size(int(text.size())) >= editor::kMaxJSString)
    return false;
  Send(tag, text.data(), int(text.size()));
  return true;
}

void WebPlugin::SendDisplay(int paramIdx)
{
  if (paramIdx < 0 || paramIdx >= NParams())
    return;
  WDL_String str;
  FormatDisplay(paramIdx, str);
  Send(paramIdx, str.Get(), str.GetLength());
}

void WebPlugin::FormatDisplay(int paramIdx, WDL_String& str) const
{
  GetParam(paramIdx)->GetDisplay(str);
}

double WebPlugin::ParseDisplay(int paramIdx, const char* text) const
{
  return GetParam(paramIdx)->StringToValue(text);
}

void WebPlugin::SetParamFromPlugin(int paramIdx, double value)
{
  const double norm = GetParam(paramIdx)->ToNormalized(value);
  BeginInformHostOfParamChangeFromUI(paramIdx);
  SendParameterValueFromUI(paramIdx, norm);
  EndInformHostOfParamChangeFromUI(paramIdx);
  /* "FromUI" goes to the host and no further; the editor has to be told. */
  SendParameterValueFromDelegate(paramIdx, value, false);
}

wire::Transport WebPlugin::HostTransport() const
{
  return wire::host_transport(GetTransportIsRunning(), GetTempo(), GetPPQPos());
}

void WebPlugin::Send(int tag, const char* data, int size)
{
  if (EditorIsOpen())
    SendArbitraryMsgFromDelegate(tag, size, data);
}

/* One message per ring, and only when there was one. The count is compared
 * with != so that its wrap is a non-event (ground.h). */
void WebPlugin::SendGround()
{
  const uint32_t fires = gnd_fires(mGround);
  if (fires == mGroundFires)
    return;
  mGroundFires = fires;
  SendText(editor::kGround, editor::EncodeGround(gnd_strength(mGround)));
}

double WebPlugin::PortDefault(int idx) const
{
  return GetParam(idx)->GetDefault(true);
}

/* A typed value is an edit like any other: through the host, into its undo
 * history and automation lane. StringToValue is the parser the host uses. */
void WebPlugin::PortSetFromText(int idx, const char* text)
{
  const double v = ParseDisplay(idx, text);
  BeginInformHostOfParamChangeFromUI(idx);
  SendParameterValueFromUI(idx, GetParam(idx)->ToNormalized(v));
  EndInformHostOfParamChangeFromUI(idx);
}

void WebPlugin::PortResize(int height)
{
  EditorResizeFromUI(GetEditorWidth(), height, true);
}

} // namespace ni
