/*
  ==============================================================================

    reverbPresets.cpp
    Shared reverb preset table + interpolation core (issue #326).

  ==============================================================================
*/

#include "reverbPresets.hpp"
#include "../utils/misc.hpp"

namespace {

  using YSE::REVERB::presetValues;

  // Indexed by REVERB_PRESET. The values are copied verbatim from the
  // historical YSE::reverb::setPreset switch so preset behaviour is
  // bit-compatible with every release before the extraction (issue #326).
  //
  // Field order: roomsize, damp, dry, wet, modFrequency, modWidth,
  //              earlyTime[4], earlyGain[4].
  constexpr presetValues presetTable[] = {
      // REVERB_OFF
      {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}},
      // REVERB_GENERIC
      {0.5f, 0.5f, 0.6f, 0.4f, 0.0f, 0.0f, {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}},
      // REVERB_PADDED
      {0.1f, 0.9f, 0.9f, 0.1f, 0.0f, 0.0f, {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}},
      // REVERB_ROOM
      {0.3f, 0.8f, 0.7f, 0.3f, 0.0f, 0.0f, {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}},
      // REVERB_BATHROOM
      {0.2f, 0.1f, 0.3f, 0.7f, 0.0f, 0.0f, {0.f, 20.f, 50.f, 85.f}, {1.f, 0.7f, 0.5f, 0.3f}},
      // REVERB_STONEROOM
      {0.3f, 0.01f, 0.3f, 0.7f, 0.0f, 0.0f, {30.f, 70.f, 100.f, 150.f}, {0.8f, 0.3f, 0.5f, 0.3f}},
      // REVERB_LARGEROOM
      {0.7f, 0.8f, 0.7f, 0.3f, 0.0f, 0.0f, {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}},
      // REVERB_HALL
      {0.7f, 0.4f, 0.5f, 0.5f, 0.0f, 0.0f, {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}},
      // REVERB_CAVE
      {1.0f, 0.3f, 0.3f, 0.7f, 0.0f, 0.0f, {100.f, 250.f, 400.f, 800.f}, {0.8f, 0.6f, 0.4f, 0.5f}},
      // REVERB_SEWERPIPE
      {0.5f,
       0.1f,
       0.3f,
       0.7f,
       3.5f,
       20.0f,
       {200.f, 600.f, 1100.f, 0.f},
       {0.05f, 0.04f, 0.01f, 0.f}},
      // REVERB_UNDERWATER
      {0.1f, 0.2f, 0.3f, 0.7f, 3.5f, 20.0f, {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}},
  };

  constexpr Int presetCount = static_cast<Int>(sizeof(presetTable) / sizeof(presetTable[0]));

} // namespace

const presetValues& YSE::REVERB::getPresetValues(REVERB_PRESET preset) {
  Int index = static_cast<Int>(preset);
  if (index < 0 || index >= presetCount) index = static_cast<Int>(REVERB_OFF);
  return presetTable[index];
}

presetValues YSE::REVERB::morph(const presetValues& a, const presetValues& b, Flt t) {
  Clamp(t, 0.f, 1.f);
  // Weighted-sum form rather than x + (y - x) * t: this one is exact at both
  // endpoints (t = 0 returns x bit-for-bit, t = 1 returns y bit-for-bit),
  // which the module's "morph 1 == pure B" contract relies on.
  const auto mix = [t](Flt x, Flt y) { return x * (1.0f - t) + y * t; };

  presetValues result;
  result.roomsize = mix(a.roomsize, b.roomsize);
  result.damp = mix(a.damp, b.damp);
  result.dry = mix(a.dry, b.dry);
  result.wet = mix(a.wet, b.wet);
  result.modFrequency = mix(a.modFrequency, b.modFrequency);
  result.modWidth = mix(a.modWidth, b.modWidth);
  for (Int i = 0; i < 4; i++) {
    result.earlyTime[i] = mix(a.earlyTime[i], b.earlyTime[i]);
    result.earlyGain[i] = mix(a.earlyGain[i], b.earlyGain[i]);
  }
  return result;
}
