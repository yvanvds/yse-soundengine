#include "yse_c/yse_reverb.h"
#include "yse_c_internal.hpp"

#include "../reverb/reverbInterface.hpp"
#include "../utils/vector.hpp"

#include <exception>

namespace {
  inline YSE::reverb* to_cpp(YseReverb* r) {
    return reinterpret_cast<YSE::reverb*>(r);
  }
  inline yse_pos_t to_c(const YSE::Pos& p) {
    yse_pos_t out;
    out.x = p.x;
    out.y = p.y;
    out.z = p.z;
    return out;
  }
  inline YSE::Pos to_cpp_pos(const yse_pos_t& p) {
    return YSE::Pos(p.x, p.y, p.z);
  }
} // namespace

extern "C" {

YSE_C_API YseReverb* yse_reverb_create(void) {
  try {
    auto* r = new YSE::reverb();
    r->create();
    return reinterpret_cast<YseReverb*>(r);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_reverb_create");
    return nullptr;
  }
}

YSE_C_API void yse_reverb_destroy(YseReverb* rev) {
  if (!rev) return;
  delete to_cpp(rev);
}

YSE_C_API int yse_reverb_is_valid(YseReverb* rev) {
  return rev && to_cpp(rev)->isValid() ? 1 : 0;
}

YSE_C_API void yse_reverb_set_position(YseReverb* rev, const yse_pos_t* p) {
  if (!rev || !p) return;
  to_cpp(rev)->setPosition(to_cpp_pos(*p));
}

YSE_C_API yse_pos_t yse_reverb_get_position(YseReverb* rev) {
  if (!rev) return yse_pos_t{0, 0, 0};
  return to_c(to_cpp(rev)->getPosition());
}

YSE_C_API void yse_reverb_set_size(YseReverb* rev, float v) {
  if (rev) to_cpp(rev)->setSize(v);
}
YSE_C_API float yse_reverb_get_size(YseReverb* rev) {
  return rev ? to_cpp(rev)->getSize() : 0.0f;
}
YSE_C_API void yse_reverb_set_roll_off(YseReverb* rev, float v) {
  if (rev) to_cpp(rev)->setRollOff(v);
}
YSE_C_API float yse_reverb_get_roll_off(YseReverb* rev) {
  return rev ? to_cpp(rev)->getRollOff() : 0.0f;
}
YSE_C_API void yse_reverb_set_active(YseReverb* rev, int on) {
  if (rev) to_cpp(rev)->setActive(on != 0);
}
YSE_C_API int yse_reverb_get_active(YseReverb* rev) {
  return rev && to_cpp(rev)->getActive() ? 1 : 0;
}
YSE_C_API void yse_reverb_set_room_size(YseReverb* rev, float v) {
  if (rev) to_cpp(rev)->setRoomSize(v);
}
YSE_C_API float yse_reverb_get_room_size(YseReverb* rev) {
  return rev ? to_cpp(rev)->getRoomSize() : 0.0f;
}
YSE_C_API void yse_reverb_set_damping(YseReverb* rev, float v) {
  if (rev) to_cpp(rev)->setDamping(v);
}
YSE_C_API float yse_reverb_get_damping(YseReverb* rev) {
  return rev ? to_cpp(rev)->getDamping() : 0.0f;
}

YSE_C_API void yse_reverb_set_dry_wet_balance(YseReverb* rev, float dry, float wet) {
  if (rev) to_cpp(rev)->setDryWetBalance(dry, wet);
}
YSE_C_API float yse_reverb_get_dry(YseReverb* rev) {
  return rev ? to_cpp(rev)->getDry() : 0.0f;
}
YSE_C_API float yse_reverb_get_wet(YseReverb* rev) {
  return rev ? to_cpp(rev)->getWet() : 0.0f;
}

YSE_C_API void yse_reverb_set_modulation(YseReverb* rev, float frequency, float width) {
  if (rev) to_cpp(rev)->setModulation(frequency, width);
}
YSE_C_API float yse_reverb_get_modulation_frequency(YseReverb* rev) {
  return rev ? to_cpp(rev)->getModulationFrequency() : 0.0f;
}
YSE_C_API float yse_reverb_get_modulation_width(YseReverb* rev) {
  return rev ? to_cpp(rev)->getModulationWidth() : 0.0f;
}

YSE_C_API void yse_reverb_set_reflection(YseReverb* rev, int reflection, int time, float gain) {
  if (rev) to_cpp(rev)->setReflection(reflection, time, gain);
}
YSE_C_API int yse_reverb_get_reflection_time(YseReverb* rev, int reflection) {
  return rev ? to_cpp(rev)->getReflectionTime(reflection) : 0;
}
YSE_C_API float yse_reverb_get_reflection_gain(YseReverb* rev, int reflection) {
  return rev ? to_cpp(rev)->getReflectionGain(reflection) : 0.0f;
}

YSE_C_API void yse_reverb_set_preset(YseReverb* rev, YseReverbPreset preset) {
  if (!rev) return;
  to_cpp(rev)->setPreset(static_cast<YSE::REVERB_PRESET>(preset));
}

} // extern "C"
