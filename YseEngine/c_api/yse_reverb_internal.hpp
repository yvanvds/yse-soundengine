/*
  yse_reverb_internal.hpp — private conversions between the C mirror struct
  YseReverbPresetValues and the engine's YSE::REVERB::presetValues, shared by
  yse_reverb.cpp (preset table / morph, issue #909) and yse_dsp_modules.cpp
  (morphing reverb endpoints). Not installed; never included by an ABI consumer.

  The ABI struct layout is never assumed to match the engine one, so every
  field is copied explicitly.
*/

#pragma once

#include "yse_c/yse_reverb.h"

#include "../reverb/reverbPresets.hpp"

namespace yse_c {

  inline YSE::REVERB::presetValues to_cpp_preset(const YseReverbPresetValues& v) {
    YSE::REVERB::presetValues p;
    p.roomsize = v.roomsize;
    p.damp = v.damp;
    p.dry = v.dry;
    p.wet = v.wet;
    p.modFrequency = v.mod_frequency;
    p.modWidth = v.mod_width;
    for (int i = 0; i < 4; ++i) {
      p.earlyTime[i] = v.early_time[i];
      p.earlyGain[i] = v.early_gain[i];
    }
    return p;
  }

  inline YseReverbPresetValues to_c_preset(const YSE::REVERB::presetValues& p) {
    YseReverbPresetValues v;
    v.roomsize = p.roomsize;
    v.damp = p.damp;
    v.dry = p.dry;
    v.wet = p.wet;
    v.mod_frequency = p.modFrequency;
    v.mod_width = p.modWidth;
    for (int i = 0; i < 4; ++i) {
      v.early_time[i] = p.earlyTime[i];
      v.early_gain[i] = p.earlyGain[i];
    }
    return v;
  }

} // namespace yse_c
