#include "yse_c/yse_sound.h"
#include "yse_c_internal.hpp"

#include "../sound/soundInterface.hpp"
#include "../channel/channelInterface.hpp"
#include "../dsp/buffer.hpp"
#include "../dsp/dspObject.hpp"
#include "../patcher/patcher.hpp"
#include "../utils/vector.hpp"

#include <exception>

namespace {
  inline YSE::sound* to_cpp(YseSound* s) {
    return reinterpret_cast<YSE::sound*>(s);
  }
  inline YSE::channel* to_cpp_chan(YseChannel* ch) {
    return reinterpret_cast<YSE::channel*>(ch);
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

YSE_C_API YseSound* yse_sound_create(void) {
  try {
    return reinterpret_cast<YseSound*>(new YSE::sound());
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_sound_create");
    return nullptr;
  }
}

YSE_C_API void yse_sound_destroy(YseSound* s) {
  if (!s) return;
  delete to_cpp(s);
}

YSE_C_API YseStatus yse_sound_load_file(YseSound* s, const char* filename, YseChannel* ch, int loop,
                                        float volume, int streaming) {
  if (!s) return YSE_ERR_INVALID_HANDLE;
  if (!filename) return YSE_ERR_INVALID_ARGUMENT;
  try {
    // YSE::sound::create() nulls pimpl when SOUND::implementation::create()
    // fails synchronously (file-not-found, format unsupported, etc.). isValid()
    // therefore reports load success, not just impl existence — see
    // soundInterface.cpp:39-51 (`pimpl = nullptr` on the failure branch).
    to_cpp(s)->create(filename, to_cpp_chan(ch), loop != 0, volume, streaming != 0);
    if (!to_cpp(s)->isValid()) {
      yse_c::set_last_error(std::string("sound load failed for: ") + filename);
      return YSE_ERR_FILE_NOT_FOUND;
    }
    return YSE_OK;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return YSE_ERR_EXCEPTION;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_sound_load_file");
    return YSE_ERR_EXCEPTION;
  }
}

YSE_C_API YseStatus yse_sound_load_buffer(YseSound* s, YseDspBuffer* buf, YseChannel* ch, int loop,
                                          float volume) {
  if (!s) return YSE_ERR_INVALID_HANDLE;
  if (!buf) return YSE_ERR_INVALID_ARGUMENT;
  try {
    auto& cpp_buf = *reinterpret_cast<YSE::DSP::buffer*>(buf);
    to_cpp(s)->create(cpp_buf, to_cpp_chan(ch), loop != 0, volume);
    if (!to_cpp(s)->isValid()) {
      yse_c::set_last_error("sound is not valid after buffer-source create");
      return YSE_ERR_GENERIC;
    }
    return YSE_OK;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return YSE_ERR_EXCEPTION;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_sound_load_buffer");
    return YSE_ERR_EXCEPTION;
  }
}

YSE_C_API YseStatus yse_sound_load_patcher(YseSound* s, YsePatcher* patch, YseChannel* ch,
                                           float volume) {
  if (!s) return YSE_ERR_INVALID_HANDLE;
  if (!patch) return YSE_ERR_INVALID_ARGUMENT;
  try {
    auto& cpp_patch = *reinterpret_cast<YSE::patcher*>(patch);
    to_cpp(s)->create(cpp_patch, to_cpp_chan(ch), volume);
    // create() refuses when the patcher is already controlled by another sound
    // (one patcher per sound — issue #287) or was never created; on refusal the
    // sound is left invalid. Mirror that as an error status for C consumers.
    if (!to_cpp(s)->isValid()) {
      yse_c::set_last_error(
          "sound not created: patcher is invalid or already controlled by another sound");
      return YSE_ERR_GENERIC;
    }
    return YSE_OK;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return YSE_ERR_EXCEPTION;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_sound_load_patcher");
    return YSE_ERR_EXCEPTION;
  }
}

YSE_C_API int yse_sound_is_valid(YseSound* s) {
  return s && to_cpp(s)->isValid() ? 1 : 0;
}
YSE_C_API int yse_sound_is_ready(YseSound* s) {
  return s && to_cpp(s)->isReady() ? 1 : 0;
}
YSE_C_API int yse_sound_is_streaming(YseSound* s) {
  return s && to_cpp(s)->isStreaming() ? 1 : 0;
}

YSE_C_API void yse_sound_play(YseSound* s) {
  if (s) to_cpp(s)->play();
}
YSE_C_API void yse_sound_pause(YseSound* s) {
  if (s) to_cpp(s)->pause();
}
YSE_C_API void yse_sound_stop(YseSound* s) {
  if (s) to_cpp(s)->stop();
}
YSE_C_API void yse_sound_toggle(YseSound* s) {
  if (s) to_cpp(s)->toggle();
}
YSE_C_API void yse_sound_restart(YseSound* s) {
  if (s) to_cpp(s)->restart();
}
YSE_C_API int yse_sound_is_playing(YseSound* s) {
  return s && to_cpp(s)->isPlaying() ? 1 : 0;
}
YSE_C_API int yse_sound_is_paused(YseSound* s) {
  return s && to_cpp(s)->isPaused() ? 1 : 0;
}
YSE_C_API int yse_sound_is_stopped(YseSound* s) {
  return s && to_cpp(s)->isStopped() ? 1 : 0;
}

YSE_C_API void yse_sound_set_pos(YseSound* s, const yse_pos_t* p) {
  if (!s || !p) return;
  to_cpp(s)->pos(to_cpp_pos(*p));
}

YSE_C_API yse_pos_t yse_sound_get_pos(YseSound* s) {
  if (!s) return yse_pos_t{0, 0, 0};
  return to_c(to_cpp(s)->pos());
}

YSE_C_API void yse_sound_set_volume(YseSound* s, float v, unsigned int fade_ms) {
  if (!s) return;
  to_cpp(s)->volume(v, fade_ms);
}
YSE_C_API float yse_sound_get_volume(YseSound* s) {
  return s ? to_cpp(s)->volume() : 0.0f;
}

YSE_C_API void yse_sound_set_speed(YseSound* s, float v) {
  if (s) to_cpp(s)->speed(v);
}
YSE_C_API float yse_sound_get_speed(YseSound* s) {
  return s ? to_cpp(s)->speed() : 0.0f;
}

YSE_C_API void yse_sound_set_size(YseSound* s, float v) {
  if (s) to_cpp(s)->size(v);
}
YSE_C_API float yse_sound_get_size(YseSound* s) {
  return s ? to_cpp(s)->size() : 0.0f;
}

YSE_C_API void yse_sound_set_spread(YseSound* s, float v) {
  if (s) to_cpp(s)->spread(v);
}
YSE_C_API float yse_sound_get_spread(YseSound* s) {
  return s ? to_cpp(s)->spread() : 0.0f;
}

YSE_C_API void yse_sound_set_looping(YseSound* s, int v) {
  if (s) to_cpp(s)->looping(v != 0);
}
YSE_C_API int yse_sound_get_looping(YseSound* s) {
  return s && to_cpp(s)->looping() ? 1 : 0;
}

YSE_C_API void yse_sound_set_relative(YseSound* s, int v) {
  if (s) to_cpp(s)->relative(v != 0);
}
YSE_C_API int yse_sound_get_relative(YseSound* s) {
  return s && to_cpp(s)->relative() ? 1 : 0;
}

YSE_C_API void yse_sound_set_doppler(YseSound* s, int v) {
  if (s) to_cpp(s)->doppler(v != 0);
}
YSE_C_API int yse_sound_get_doppler(YseSound* s) {
  return s && to_cpp(s)->doppler() ? 1 : 0;
}

YSE_C_API void yse_sound_set_pan2d(YseSound* s, int v) {
  if (s) to_cpp(s)->pan2D(v != 0);
}
YSE_C_API int yse_sound_get_pan2d(YseSound* s) {
  return s && to_cpp(s)->pan2D() ? 1 : 0;
}

YSE_C_API void yse_sound_set_occlusion(YseSound* s, int v) {
  if (s) to_cpp(s)->occlusion(v != 0);
}
YSE_C_API int yse_sound_get_occlusion(YseSound* s) {
  return s && to_cpp(s)->occlusion() ? 1 : 0;
}

YSE_C_API void yse_sound_fade_and_stop(YseSound* s, unsigned int time_ms) {
  if (s) to_cpp(s)->fadeAndStop(time_ms);
}

YSE_C_API void yse_sound_set_time(YseSound* s, float samples) {
  if (s) to_cpp(s)->time(samples);
}
YSE_C_API float yse_sound_get_time(YseSound* s) {
  return s ? to_cpp(s)->time() : 0.0f;
}

YSE_C_API unsigned int yse_sound_length(YseSound* s) {
  return s ? to_cpp(s)->length() : 0u;
}

YSE_C_API void yse_sound_move_to(YseSound* s, YseChannel* target) {
  if (!s || !target) return;
  to_cpp(s)->moveTo(*to_cpp_chan(target));
}

YSE_C_API void yse_sound_set_dsp(YseSound* s, YseDspObject* dsp) {
  if (!s) return;
  to_cpp(s)->setDSP(reinterpret_cast<YSE::DSP::dspObject*>(dsp));
}

YSE_C_API YseDspObject* yse_sound_get_dsp(YseSound* s) {
  if (!s) return nullptr;
  return reinterpret_cast<YseDspObject*>(to_cpp(s)->getDSP());
}

} // extern "C"
