/*
 * The Trance Gate's fifteen host parameters: their order, and how each is
 * declared.
 * Copyright (c) 2026 Torben Gräber. MIT -- see LICENSE.
 *
 * Apart from the plugin class so a test can reach them: tests/cpp/tg_params.cpp
 * declares these on bare iplug::IParams -- the class the plugin uses -- and
 * checks every value survives value -> text -> value, the round trip a CLAP
 * host (and clap-validator's param-conversions) makes.
 */
#pragma once

#include "IPlugParameter.h"

#include <functional>

/*
 * THE FIFTEEN AUTOMATABLE VALUES, IN THE ENGINE'S OWN WIRE ORDER.
 *
 * Deliberately tg_param_t's order, so the host index IS the engine index and
 * there is no mapping table between them to get wrong.
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
