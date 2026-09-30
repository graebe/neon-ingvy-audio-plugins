/*
 * The Trance Gate's fifteen host parameters: their order, and how each is
 * declared.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Lifted out of TranceGate.cpp for the reason Wire.cpp and Patch.cpp were: a
 * test cannot link the plugin's own translation unit, and a parameter's display
 * text is something a host parses back. tests/cpp/tg_params.cpp declares these
 * on bare iplug::IParams -- the same class the plugin uses -- and checks that
 * every value survives value -> text -> value, the round trip a CLAP host (and
 * clap-validator's param-conversions) makes.
 */
#pragma once

#include "IPlugParameter.h"

#include <functional>

/*
 * THE FIFTEEN AUTOMATABLE VALUES, IN THE ENGINE'S OWN WIRE ORDER.
 *
 * This enum is deliberately tg_param_t's order, so the host index IS the
 * engine index and there is no mapping table between them to get wrong. The
 * JUCE build carried exactly such a table (a `wire[]` array) because its
 * parameter declaration order had drifted from the engine's; starting again
 * is a chance not to.
 */
enum EParams
{
  kSlot = 0,
  kLength,
  kRate,
  kLegato,
  kTimeMode,
  kCurve,
  kAmount,
  kWidth,
  kAttack,
  kDecay,
  kSustain,
  kRelease,
  /* APPENDED, like the engine's own two. A host that catalogued this plugin
   * stored these indices, so the fade's pair can only go on the end -- not
   * beside kAmount, which is where they belong by meaning. */
  kFade,
  kFadeSoft,
  kFadeDir,
  kNumParams
};

namespace tg {
namespace params {

/* Declares every parameter on the IParam `param(i)` returns for index i. */
void Declare(const std::function<iplug::IParam*(int)>& param);

} // namespace params
} // namespace tg
