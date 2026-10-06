// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A plugin's parameters, hosted the way iPlug2 hosts them -- without a plugin.
 *
 * The plugin class cannot be built outside a bundle (IPlug_include_in_plug_hdr.h
 * #errors), but its parameters can: they are iplug::IParam, which links on its
 * own with IPlugParameter.cpp. So a test declares them with the plugin's own
 * Params.cpp onto real IParams and drives them as a host would:
 *
 *   SerializeParams / UnserializeParams   IPluginBase's, line for line: one
 *                                         double per parameter, in order
 *   clap_to_text / clap_from_text         IPlugCLAP's paramsValueToText and
 *                                         paramsTextToValue: a double is
 *                                         normalised on the wire, a stepped
 *                                         parameter plain, and the label is
 *                                         appended after the display text
 *   Randomize                             clap-validator's ParamFuzzer: a
 *                                         stepped parameter gets a whole
 *                                         number in range, a double any value
 *
 * Header-only on purpose: each test binary is two or three files.
 */
#pragma once

#include "IPlugParameter.h"
#include "IPlugStructs.h"

#include <memory>
#include <random>
#include <string>
#include <vector>

namespace test {

struct ParamHost
{
  std::vector<std::unique_ptr<iplug::IParam>> params;

  explicit ParamHost(int n)
  {
    for (int i = 0; i < n; i++)
      params.push_back(std::make_unique<iplug::IParam>());
  }

  int NParams() const { return int(params.size()); }
  iplug::IParam* GetParam(int i) const { return params[size_t(i)].get(); }

  bool SerializeParams(iplug::IByteChunk& chunk) const
  {
    bool ok = true;
    for (int i = 0; i < NParams() && ok; i++)
    {
      double v = GetParam(i)->Value();
      ok &= chunk.Put(&v) > 0;
    }
    return ok;
  }

  int UnserializeParams(const iplug::IByteChunk& chunk, int pos)
  {
    for (int i = 0; i < NParams() && pos >= 0; i++)
    {
      double v = 0.0;
      pos = chunk.Get(&v, pos);
      if (pos >= 0) GetParam(i)->Set(v);
    }
    return pos;
  }

  void Randomize(std::mt19937& rng)
  {
    for (auto& p : params)
    {
      if (p->Type() == iplug::IParam::kTypeDouble)
        p->SetNormalized(std::uniform_real_distribution<double>(0.0, 1.0)(rng));
      else
        p->Set(double(std::uniform_int_distribution<int>(int(p->GetMin()), int(p->GetMax()))(rng)));
    }
  }

  std::vector<double> Values() const
  {
    std::vector<double> out;
    for (auto& p : params) out.push_back(p->Value());
    return out;
  }
};

inline bool is_double(const iplug::IParam& p) { return p.Type() == iplug::IParam::kTypeDouble; }

/* IPlugCLAP::paramsValueToText. */
inline std::string clap_to_text(const iplug::IParam& p, double clapValue)
{
  WDL_String s;
  p.GetDisplay(clapValue, is_double(p), s);
  if (iplug::CStringHasContents(p.GetLabel()))
  {
    s.Append(" ");
    s.Append(p.GetLabel());
  }
  return s.Get();
}

/* IPlugCLAP::paramsTextToValue. */
inline double clap_from_text(const iplug::IParam& p, const std::string& text)
{
  const double v = p.StringToValue(text.c_str());
  return is_double(p) ? p.ToNormalized(v) : v;
}

/* clap-validator's range for a parameter: normalised for a double, plain
 * otherwise -- what paramsInfo reports. */
inline double clap_min(const iplug::IParam& p) { return is_double(p) ? 0.0 : p.GetMin(); }
inline double clap_max(const iplug::IParam& p) { return is_double(p) ? 1.0 : p.GetMax(); }

/*
 * param-conversions, for one parameter at one value: text, back to a value, and
 * to text again must agree, and so must the value after one more hop. Returns
 * "" when it round-trips, else what the validator would print.
 */
inline std::string round_trip(const iplug::IParam& p, double value)
{
  const std::string t0 = clap_to_text(p, value);
  const double v1 = clap_from_text(p, t0);
  const std::string t1 = clap_to_text(p, v1);
  const double v2 = clap_from_text(p, t1);
  if (t0 == t1 && v1 == v2) return "";
  return std::string(p.GetName()) + ": " + std::to_string(value) + " -> '" + t0 + "' -> " +
         std::to_string(v1) + " -> '" + t1 + "' -> " + std::to_string(v2);
}

} // namespace test
