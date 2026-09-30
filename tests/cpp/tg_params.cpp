/*
 * Every Trance Gate parameter survives value -> text -> value.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * A host shows a parameter as the plugin's text and parses what a user types
 * back through the plugin, so the two have to invert each other; this is
 * clap-validator's param-conversions, on the plugin's own declarations
 * (Params.cpp) and the CLAP wrapper's own conversions (param_host.h).
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "Params.h"
#include "param_host.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

test::ParamHost declared()
{
  test::ParamHost host(kNumParams);
  tg::params::Declare([&](int i) { return host.GetParam(i); });
  return host;
}

/* The validator's sweep: evenly across the range, as many points per parameter
 * as 4000 spread over them allows, between 5 and 100. */
int sweep_points(int nParams)
{
  return std::clamp((4000 + nParams - 1) / nParams, 5, 100);
}

} // namespace

TEST_CASE("every value a host can hold survives value -> text -> value")
{
  const test::ParamHost host = declared();
  const int n = sweep_points(host.NParams());
  for (int i = 0; i < host.NParams(); i++)
  {
    const iplug::IParam& p = *host.GetParam(i);
    for (int k = 0; k < n; k++)
    {
      double v = test::clap_min(p) + (test::clap_max(p) - test::clap_min(p)) * k / (n - 1);
      /* A stepped parameter holds whole numbers; see the case below for the
       * values in between. */
      if (!test::is_double(p)) v = std::round(v);
      CHECK_MESSAGE(test::round_trip(p, v) == "", test::round_trip(p, v));
    }
  }
}

TEST_CASE("a unit is shown once")
{
  /* The CLAP wrapper appends the label after the display text, so a unit in
   * both printed "100.00 % %". */
  const test::ParamHost host = declared();
  for (int i = 0; i < host.NParams(); i++)
  {
    const iplug::IParam& p = *host.GetParam(i);
    const std::string label = p.GetLabel();
    if (label.empty()) continue;
    WDL_String bare;
    p.GetDisplay(bare);
    const std::string text = bare.Get();
    CHECK_MESSAGE(!(text.size() >= label.size() &&
                    text.compare(text.size() - label.size(), label.size(), label) == 0),
                  p.GetName(), " says its unit twice: '", test::clap_to_text(p, p.Value()), "'");
  }
}

/*
 * A STEPPED PARAMETER BETWEEN ITS STEPS -- expected to fail until iPlug2 rounds
 * before it looks up an enum's text.
 *
 * clap-validator sweeps a stepped parameter's range continuously, so it asks
 * for the text of Rate at 0.12. IParam::GetDisplay compares that EXACTLY with
 * the enum's values, finds none, and prints "0"; "0" is no label, so parsing it
 * lands on the first entry, "1/1T". The parameter itself would have held 0 --
 * Set() rounds -- so the fix is one line in IParam::GetDisplay (Constrain the
 * value as FromNormalized already does), and it is upstream's to make. When
 * it lands this case starts passing, doctest reports that as a failure, and
 * the decorator comes off.
 */
TEST_CASE("a stepped parameter between its steps shows the step it would hold"
          * doctest::should_fail())
{
  const test::ParamHost host = declared();
  const iplug::IParam& rate = *host.GetParam(kRate);
  CHECK(test::round_trip(rate, 0.12) == "");
  CHECK(test::clap_to_text(rate, 0.12) == test::clap_to_text(rate, 0.0));
}
