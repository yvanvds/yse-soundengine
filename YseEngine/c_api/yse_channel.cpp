#include "yse_c/yse_channel.h"
#include "yse_c_internal.hpp"

#include "../channel/channelInterface.hpp"

#include <exception>

namespace {
  inline YSE::channel* to_cpp(YseChannel* ch) {
    return reinterpret_cast<YSE::channel*>(ch);
  }

  inline YseChannel* to_c(YSE::channel& ch) {
    return reinterpret_cast<YseChannel*>(&ch);
  }
}

extern "C" {

YSE_C_API YseChannel* yse_channel_master(void)  { return to_c(YSE::ChannelMaster()); }
YSE_C_API YseChannel* yse_channel_fx(void)      { return to_c(YSE::ChannelFX()); }
YSE_C_API YseChannel* yse_channel_music(void)   { return to_c(YSE::ChannelMusic()); }
YSE_C_API YseChannel* yse_channel_ambient(void) { return to_c(YSE::ChannelAmbient()); }
YSE_C_API YseChannel* yse_channel_voice(void)   { return to_c(YSE::ChannelVoice()); }
YSE_C_API YseChannel* yse_channel_gui(void)     { return to_c(YSE::ChannelGui()); }

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

YSE_C_API void yse_channel_destroy(YseChannel* ch) {
  if (!ch) return;
  delete to_cpp(ch);
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

} // extern "C"
