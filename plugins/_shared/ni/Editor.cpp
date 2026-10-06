// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * ni::editor. See Editor.h.
 */
#include "ni/Editor.h"
#include "ni/Wire.h"

namespace ni {
namespace editor {

std::string EncodeDefaults(const Port& port)
{
  std::string out;
  const int n = port.PortParamCount();
  out.reserve(size_t(n) * 12);
  for (int i = 0; i < n; i++)
  {
    if (i)
      out += ':';
    wire::append_decimal(out, port.PortDefault(i), 9);
  }
  return out;
}

std::string EncodeGround(float strength)
{
  std::string out;
  wire::append_fixed(out, double(strength), 3);
  return out;
}

void SendValues(Port& port)
{
  port.PortSendValues();
  for (int i = 0; i < port.PortParamCount(); i++)
    port.PortSendDisplay(i);
}

void SendAll(Port& port)
{
  const std::string defaults = EncodeDefaults(port);
  port.PortSend(kDefaults, defaults.data(), int(defaults.size()));
  SendValues(port);
  port.PortReady();
}

bool Handle(Port& port, int tag, std::string_view arg)
{
  switch (tag)
  {
    case kReady:
      SendAll(port);
      return true;

    case kSetText:
    {
      std::string idx, text;
      int i = -1;
      if (wire::split_pair(arg, idx, text) && wire::parse_int(idx, i) && i >= 0 &&
          i < port.PortParamCount())
      {
        port.PortSetFromText(i, text.c_str());
        port.PortSendDisplay(i);
      }
      return true;
    }

    case kGroundRun:
      port.PortGroundRunning(arg == "1");
      return true;

    case kHeight:
    {
      int h = 0;
      if (wire::parse_int(arg, h))
        h = wire::clamp_editor_height(h);
      if (h > 0 && h != port.PortHeight())
        port.PortResize(h);
      return true;
    }

    default:
      return false;
  }
}

} // namespace editor
} // namespace ni
