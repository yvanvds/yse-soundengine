#include "yse_c/yse_system.h"
#include "yse_c_internal.hpp"

#include "../system.hpp"

#include <exception>

namespace {
  inline YSE::system* to_cpp(YseSystem* s) {
    return reinterpret_cast<YSE::system*>(s);
  }
}

extern "C" {

YSE_C_API YseSystem* yse_system_get(void) {
  return reinterpret_cast<YseSystem*>(&YSE::System());
}

YSE_C_API YseStatus yse_system_init(YseSystem* sys) {
  if (!sys) return YSE_ERR_INVALID_HANDLE;
  try {
    if (!to_cpp(sys)->init()) {
      yse_c::set_last_error("System().init() returned false (no audio device available)");
      return YSE_ERR_AUDIO_DEVICE;
    }
    return YSE_OK;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return YSE_ERR_EXCEPTION;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_system_init");
    return YSE_ERR_EXCEPTION;
  }
}

YSE_C_API YseStatus yse_system_init_offline(YseSystem* sys) {
  if (!sys) return YSE_ERR_INVALID_HANDLE;
  try {
    if (!to_cpp(sys)->initOffline()) {
      yse_c::set_last_error("System().initOffline() returned false");
      return YSE_ERR_GENERIC;
    }
    return YSE_OK;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return YSE_ERR_EXCEPTION;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_system_init_offline");
    return YSE_ERR_EXCEPTION;
  }
}

YSE_C_API void yse_system_render_offline(YseSystem* sys, int blocks) {
  if (!sys) return;
  to_cpp(sys)->renderOffline(blocks);
}

YSE_C_API void yse_system_update(YseSystem* sys) {
  if (!sys) return;
  to_cpp(sys)->update();
}

YSE_C_API void yse_system_close(YseSystem* sys) {
  if (!sys) return;
  to_cpp(sys)->close();
}

YSE_C_API void yse_system_pause(YseSystem* sys) {
  if (!sys) return;
  to_cpp(sys)->pause();
}

YSE_C_API void yse_system_resume(YseSystem* sys) {
  if (!sys) return;
  to_cpp(sys)->resume();
}

YSE_C_API int yse_system_missed_callbacks(YseSystem* sys) {
  if (!sys) return 0;
  return to_cpp(sys)->missedCallbacks();
}

YSE_C_API float yse_system_cpu_load(YseSystem* sys) {
  if (!sys) return 0.0f;
  return to_cpp(sys)->cpuLoad();
}

YSE_C_API void yse_system_sleep(YseSystem* sys, unsigned int ms) {
  if (!sys) return;
  to_cpp(sys)->sleep(ms);
}

YSE_C_API void yse_system_set_max_sounds(YseSystem* sys, int value) {
  if (!sys) return;
  to_cpp(sys)->maxSounds(value);
}

YSE_C_API int yse_system_get_max_sounds(YseSystem* sys) {
  if (!sys) return 0;
  return to_cpp(sys)->maxSounds();
}

YSE_C_API void yse_system_audio_test(YseSystem* sys, int on) {
  if (!sys) return;
  to_cpp(sys)->AudioTest(on != 0);
}

YSE_C_API void yse_system_auto_reconnect(YseSystem* sys, int on, int delay_ms) {
  if (!sys) return;
  to_cpp(sys)->autoReconnect(on != 0, delay_ms);
}

} // extern "C"
