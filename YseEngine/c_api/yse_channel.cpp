#include "yse_c/yse_channel.h"
#include "yse_c_internal.hpp"

#include "../channel/channelInterface.hpp"
#include "../dsp/dspObject.hpp"

#include <exception>

namespace {
  inline YSE::channel* to_cpp(YseChannel* ch) {
    return reinterpret_cast<YSE::channel*>(ch);
  }

  inline YseChannel* to_c(YSE::channel& ch) {
    return reinterpret_cast<YseChannel*>(&ch);
  }
} // namespace

extern "C" {

YSE_C_API YseChannel* yse_channel_master(void) {
  return to_c(YSE::ChannelMaster());
}
YSE_C_API YseChannel* yse_channel_fx(void) {
  return to_c(YSE::ChannelFX());
}
YSE_C_API YseChannel* yse_channel_music(void) {
  return to_c(YSE::ChannelMusic());
}
YSE_C_API YseChannel* yse_channel_ambient(void) {
  return to_c(YSE::ChannelAmbient());
}
YSE_C_API YseChannel* yse_channel_voice(void) {
  return to_c(YSE::ChannelVoice());
}
YSE_C_API YseChannel* yse_channel_gui(void) {
  return to_c(YSE::ChannelGui());
}

YSE_C_API YseChannel* yse_channel_create(const char* name, YseChannel* parent) {
  if (!name || !parent) {
    yse_c::set_last_error("yse_channel_create: name and parent must be non-null");
    return nullptr;
  }
  try {
    auto* ch = new YSE::channel();
    ch->create(name, *to_cpp(parent));
    return reinterpret_cast<YseChannel*>(ch);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_channel_create");
    return nullptr;
  }
}

YSE_C_API YseChannel* yse_channel_create_with_sends(const char* name, YseChannel* parent,
                                                    int send_slots) {
  if (!name || !parent) {
    yse_c::set_last_error("yse_channel_create_with_sends: name and parent must be non-null");
    return nullptr;
  }
  try {
    auto* ch = new YSE::channel();
    ch->create(name, *to_cpp(parent), send_slots);
    return reinterpret_cast<YseChannel*>(ch);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_channel_create_with_sends");
    return nullptr;
  }
}

YSE_C_API YseChannel* yse_channel_create_return(const char* name, int send_slots) {
  if (!name) {
    yse_c::set_last_error("yse_channel_create_return: name must be non-null");
    return nullptr;
  }
  try {
    auto* ch = new YSE::channel();
    ch->makeReturn(name, send_slots);
    return reinterpret_cast<YseChannel*>(ch);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_channel_create_return");
    return nullptr;
  }
}

YSE_C_API void yse_channel_destroy(YseChannel* ch) {
  if (!ch) return;
  delete to_cpp(ch);
}

YSE_C_API int yse_channel_is_return(YseChannel* ch) {
  if (!ch) return 0;
  return to_cpp(ch)->isReturn() ? 1 : 0;
}

YSE_C_API void yse_channel_send(YseChannel* ch, int slot, YseChannel* return_bus, float level,
                                int pre_fader) {
  if (!ch || !return_bus) return;
  to_cpp(ch)->send(slot, *to_cpp(return_bus), level, pre_fader != 0);
}

YSE_C_API void yse_channel_set_send_level(YseChannel* ch, int slot, float level) {
  if (!ch) return;
  to_cpp(ch)->setSendLevel(slot, level);
}

YSE_C_API void yse_channel_clear_send(YseChannel* ch, int slot) {
  if (!ch) return;
  to_cpp(ch)->clearSend(slot);
}

YSE_C_API float yse_channel_get_send_level(YseChannel* ch, int slot) {
  if (!ch) return 0.0f;
  return to_cpp(ch)->getSendLevel(slot);
}

YSE_C_API void yse_channel_set_dsp(YseChannel* ch, YseDspObject* dsp) {
  if (!ch) return;
  to_cpp(ch)->setDSP(reinterpret_cast<YSE::DSP::dspObject*>(dsp));
}

YSE_C_API YseDspObject* yse_channel_get_dsp(YseChannel* ch) {
  if (!ch) return nullptr;
  return reinterpret_cast<YseDspObject*>(to_cpp(ch)->getDSP());
}

YSE_C_API void yse_channel_set_volume(YseChannel* ch, float value) {
  if (!ch) return;
  to_cpp(ch)->setVolume(value);
}

YSE_C_API float yse_channel_get_volume(YseChannel* ch) {
  if (!ch) return 0.0f;
  return to_cpp(ch)->getVolume();
}

YSE_C_API void yse_channel_move_to(YseChannel* ch, YseChannel* parent) {
  if (!ch || !parent) return;
  to_cpp(ch)->moveTo(*to_cpp(parent));
}

YSE_C_API void yse_channel_attach_reverb(YseChannel* ch) {
  if (!ch) return;
  to_cpp(ch)->attachReverb();
}

YSE_C_API void yse_channel_set_virtual(YseChannel* ch, int value) {
  if (!ch) return;
  to_cpp(ch)->setVirtual(value != 0);
}

YSE_C_API int yse_channel_get_virtual(YseChannel* ch) {
  if (!ch) return 0;
  return to_cpp(ch)->getVirtual() ? 1 : 0;
}

YSE_C_API int yse_channel_is_valid(YseChannel* ch) {
  if (!ch) return 0;
  return to_cpp(ch)->isValid() ? 1 : 0;
}

YSE_C_API const char* yse_channel_get_name(YseChannel* ch) {
  if (!ch) return "";
  return to_cpp(ch)->getName();
}

YSE_C_API int yse_channel_get_num_outputs(YseChannel* ch) {
  if (!ch) return 0;
  return to_cpp(ch)->getNumOutputs();
}

YSE_C_API float yse_channel_get_peak_linear_pre(YseChannel* ch) {
  if (!ch) return 0.f;
  return to_cpp(ch)->getPeakLinearPre();
}

YSE_C_API float yse_channel_get_peak_linear_post(YseChannel* ch) {
  if (!ch) return 0.f;
  return to_cpp(ch)->getPeakLinearPost();
}

YSE_C_API float yse_channel_get_peak_db_pre(YseChannel* ch) {
  if (!ch) return 0.f;
  return to_cpp(ch)->getPeakDbPre();
}

YSE_C_API float yse_channel_get_peak_db_post(YseChannel* ch) {
  if (!ch) return 0.f;
  return to_cpp(ch)->getPeakDbPost();
}

YSE_C_API float yse_channel_get_peak_linear_pre_output(YseChannel* ch, int output_idx) {
  if (!ch) return 0.f;
  return to_cpp(ch)->getPeakLinearPre(output_idx);
}

YSE_C_API float yse_channel_get_peak_linear_post_output(YseChannel* ch, int output_idx) {
  if (!ch) return 0.f;
  return to_cpp(ch)->getPeakLinearPost(output_idx);
}

YSE_C_API float yse_channel_get_peak_db_pre_output(YseChannel* ch, int output_idx) {
  if (!ch) return 0.f;
  return to_cpp(ch)->getPeakDbPre(output_idx);
}

YSE_C_API float yse_channel_get_peak_db_post_output(YseChannel* ch, int output_idx) {
  if (!ch) return 0.f;
  return to_cpp(ch)->getPeakDbPost(output_idx);
}

} // extern "C"
