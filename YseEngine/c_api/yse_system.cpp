#include "yse_c/yse_system.h"
#include "yse_c_internal.hpp"

#include "../system.hpp"
#include "../channel/channelInterface.hpp"
#include "../reverb/reverbInterface.hpp"
#include "../device/deviceInterface.hpp"
#include "../device/deviceSetup.hpp"

#include <cstring>
#include <exception>
#include <string>

namespace {
  inline YSE::system* to_cpp(YseSystem* s) {
    return reinterpret_cast<YSE::system*>(s);
  }
  inline const YSE::deviceSetup* to_cpp(const YseDeviceSetup* s) {
    return reinterpret_cast<const YSE::deviceSetup*>(s);
  }
  inline const YSE::channel* to_cpp(const YseChannel* c) {
    return reinterpret_cast<const YSE::channel*>(c);
  }

  size_t copy_string(const std::string& src, char* buf, size_t cap) {
    if (buf != nullptr && cap > 0) {
      const size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
      std::memcpy(buf, src.data(), n);
      buf[n] = '\0';
    }
    return src.size();
  }
} // namespace

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

YSE_C_API double yse_system_get_sample_rate(YseSystem* sys) {
  if (!sys) return 0.0;
  return to_cpp(sys)->getSampleRate();
}

YSE_C_API double yse_system_get_active_sample_rate(YseSystem* sys) {
  if (!sys) return 0.0;
  return to_cpp(sys)->getActiveSampleRate();
}

YSE_C_API int yse_system_get_active_buffer_size(YseSystem* sys) {
  if (!sys) return 0;
  return to_cpp(sys)->getActiveBufferSize();
}

YSE_C_API int yse_system_get_active_output_latency(YseSystem* sys) {
  if (!sys) return 0;
  return to_cpp(sys)->getActiveOutputLatency();
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

YSE_C_API int yse_system_create_clock(YseSystem* sys, const char* name, float initial_tempo) {
  if (!sys || !name) return 0;
  try {
    return to_cpp(sys)->createClock(name, initial_tempo) ? 1 : 0;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return 0;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_system_create_clock");
    return 0;
  }
}

YSE_C_API void yse_system_destroy_clock(YseSystem* sys, const char* name) {
  if (!sys || !name) return;
  try {
    to_cpp(sys)->destroyClock(name);
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_system_destroy_clock");
  }
}

YSE_C_API int yse_system_clock_exists(YseSystem* sys, const char* name) {
  if (!sys || !name) return 0;
  try {
    return to_cpp(sys)->clockExists(name) ? 1 : 0;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_system_clock_exists");
    return 0;
  }
}

YSE_C_API void yse_system_set_tempo(YseSystem* sys, const char* name, float bpm,
                                    float ramp_seconds) {
  if (!sys || !name) return;
  try {
    to_cpp(sys)->setTempo(name, bpm, ramp_seconds);
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_system_set_tempo");
  }
}

YSE_C_API double yse_system_beat_position(YseSystem* sys, const char* name) {
  if (!sys || !name) return 0.0;
  try {
    return to_cpp(sys)->beatPosition(name);
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_system_beat_position");
    return 0.0;
  }
}

YSE_C_API float yse_system_current_tempo(YseSystem* sys, const char* name) {
  if (!sys || !name) return 0.0f;
  try {
    return to_cpp(sys)->currentTempo(name);
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_system_current_tempo");
    return 0.0f;
  }
}

YSE_C_API void yse_system_audio_test(YseSystem* sys, int on) {
  if (!sys) return;
  to_cpp(sys)->AudioTest(on != 0);
}

YSE_C_API void yse_system_auto_reconnect(YseSystem* sys, int on, int delay_ms) {
  if (!sys) return;
  to_cpp(sys)->autoReconnect(on != 0, delay_ms);
}

// ─── devices ───────────────────────────────────────────────────────────────

YSE_C_API unsigned int yse_system_num_devices(YseSystem* sys) {
  if (!sys) return 0;
  return to_cpp(sys)->getNumDevices();
}

YSE_C_API YseDevice* yse_system_get_device(YseSystem* sys, unsigned int idx) {
  if (!sys) return nullptr;
  try {
    const YSE::device& d = to_cpp(sys)->getDevice(idx);
    // The engine owns the storage; we hand out a borrowed mutable view.
    return reinterpret_cast<YseDevice*>(const_cast<YSE::device*>(&d));
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_system_get_device");
    return nullptr;
  }
}

YSE_C_API YseStatus yse_system_open_device(YseSystem* sys, const YseDeviceSetup* setup,
                                           YseChannelType layout) {
  if (!sys) return YSE_ERR_INVALID_HANDLE;
  if (!setup) return YSE_ERR_INVALID_ARGUMENT;
  try {
    to_cpp(sys)->openDevice(*to_cpp(setup), static_cast<YSE::CHANNEL_TYPE>(layout));
    return YSE_OK;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return YSE_ERR_AUDIO_DEVICE;
  } catch (...) {
    yse_c::set_last_error("unknown C++ exception in yse_system_open_device");
    return YSE_ERR_AUDIO_DEVICE;
  }
}

YSE_C_API void yse_system_close_current_device(YseSystem* sys) {
  if (!sys) return;
  to_cpp(sys)->closeCurrentDevice();
}

YSE_C_API size_t yse_system_default_device(YseSystem* sys, char* buf, size_t cap) {
  if (!sys) {
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
  }
  return copy_string(to_cpp(sys)->getDefaultDevice(), buf, cap);
}

YSE_C_API size_t yse_system_default_host(YseSystem* sys, char* buf, size_t cap) {
  if (!sys) {
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
  }
  return copy_string(to_cpp(sys)->getDefaultHost(), buf, cap);
}

// ─── MIDI device enumeration ───────────────────────────────────────────────

YSE_C_API unsigned int yse_system_num_midi_in_devices(YseSystem* sys) {
#if YSE_ENABLE_MIDI_DEVICE
  if (!sys) return 0;
  return to_cpp(sys)->getNumMidiInDevices();
#else
  (void)sys;
  return 0;
#endif
}

YSE_C_API unsigned int yse_system_num_midi_out_devices(YseSystem* sys) {
#if YSE_ENABLE_MIDI_DEVICE
  if (!sys) return 0;
  return to_cpp(sys)->getNumMidiOutDevices();
#else
  (void)sys;
  return 0;
#endif
}

YSE_C_API size_t yse_system_midi_in_device_name(YseSystem* sys, unsigned int id, char* buf,
                                                size_t cap) {
  if (buf && cap > 0) buf[0] = '\0';
#if YSE_ENABLE_MIDI_DEVICE
  if (!sys) return 0;
  try {
    return copy_string(to_cpp(sys)->getMidiInDeviceName(id), buf, cap);
  } catch (const std::exception&) {
    return 0;
  }
#else
  (void)sys;
  (void)id;
  (void)buf;
  (void)cap;
  return 0;
#endif
}

YSE_C_API size_t yse_system_midi_out_device_name(YseSystem* sys, unsigned int id, char* buf,
                                                 size_t cap) {
  if (buf && cap > 0) buf[0] = '\0';
#if YSE_ENABLE_MIDI_DEVICE
  if (!sys) return 0;
  try {
    return copy_string(to_cpp(sys)->getMidiOutDeviceName(id), buf, cap);
  } catch (const std::exception&) {
    return 0;
  }
#else
  (void)sys;
  (void)id;
  (void)buf;
  (void)cap;
  return 0;
#endif
}

// ─── global reverb ─────────────────────────────────────────────────────────

YSE_C_API YseReverb* yse_system_get_global_reverb(YseSystem* sys) {
  if (!sys) return nullptr;
  return reinterpret_cast<YseReverb*>(&to_cpp(sys)->getGlobalReverb());
}

// ─── underwater FX ─────────────────────────────────────────────────────────

YSE_C_API void yse_system_underwater_fx(YseSystem* sys, const YseChannel* target) {
  if (!sys || !target) return;
  to_cpp(sys)->underWaterFX(*to_cpp(target));
}

YSE_C_API void yse_system_set_underwater_depth(YseSystem* sys, float depth) {
  if (!sys) return;
  to_cpp(sys)->setUnderWaterDepth(depth);
}

} // extern "C"
