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

/* --------------------------------------------------------------- stages */

TEST_CASE("a stage reads in the unit Env Time asks for")
{
  /* 16 % of a 93.75 ms gate (a 1/16 step at 120 BPM, Width 75 %). */
  CHECK(tg::params::FormatStage(16.0, true, 93.75) == "15.0 ms");
  CHECK(tg::params::FormatStage(16.0, false, 93.75) == "16.00 %");
  /* Before the engine has published a width there are no milliseconds to show. */
  CHECK(tg::params::FormatStage(16.0, true, 0.0) == "16.00 %");
  CHECK(tg::params::IsStage(kAttack));
  CHECK(tg::params::IsStage(kRelease));
  CHECK_FALSE(tg::params::IsStage(kSustain));
}

/*
 * THE BUG: in ms mode the editor built "12.5 ms" itself and handed what was
 * typed to the percent parser, so 12.5 typed into an ms readout set 12.5 %.
 */
TEST_CASE("text typed in ms mode is milliseconds")
{
  CHECK(tg::params::ParseStage("12.5", true, 93.75) == doctest::Approx(12.5 / 93.75 * 100.0));
  CHECK(tg::params::ParseStage("12.5 ms", true, 93.75) == doctest::Approx(12.5 / 93.75 * 100.0));
  /* An explicit unit wins over the mode, both ways round. */
  CHECK(tg::params::ParseStage("25 %", true, 93.75) == doctest::Approx(25.0));
  CHECK(tg::params::ParseStage("40 ms", false, 80.0) == doctest::Approx(50.0));
  CHECK(tg::params::ParseStage("25", false, 93.75) == doctest::Approx(25.0));
  /* Clamped to the stage's range; a comma locale does not matter. */
  CHECK(tg::params::ParseStage("1000 ms", true, 93.75) == doctest::Approx(200.0));
  CHECK(tg::params::ParseStage("-3", false, 93.75) == doctest::Approx(0.0));
}

TEST_CASE("a stage's ms text reads back to the percent it came from")
{
  for (double pct : {0.0, 1.6, 16.0, 99.9, 200.0})
  {
    const std::string t = tg::params::FormatStage(pct, true, 93.75);
    /* One decimal of a millisecond is the readout's resolution. */
    CHECK(tg::params::ParseStage(t.c_str(), true, 93.75) ==
          doctest::Approx(pct).epsilon(0.001).scale(100));
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
