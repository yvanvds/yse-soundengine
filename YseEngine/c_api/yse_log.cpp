#include "yse_c/yse_log.h"
#include "yse_c_internal.hpp"

#include "../log.hpp"
#include "../headers/enums.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
  inline YSE::log* to_cpp(YseLog* l) {
    return reinterpret_cast<YSE::log*>(l);
  }

  size_t copy_string(const char* src, char* buf, size_t cap) {
    if (!src) src = "";
    const size_t n_full = std::strlen(src);
    if (buf != nullptr && cap > 0) {
      const size_t n = n_full < cap - 1 ? n_full : cap - 1;
      std::memcpy(buf, src, n);
      buf[n] = '\0';
    }
    return n_full;
  }

  // Bridge from YSE's virtual logHandler to a C function pointer.
  // Only one bridge instance exists for the singleton Log(); installing
  // a new callback replaces the slot. The callback + user_data pair lives
  // as atomics so AddMessage (called from whichever thread emitted the
  // log) never blocks on installer state — mirrors the pattern used by
  // the midiIn raw bridge in yse_midi.cpp. See issue #58.
  class CallbackBridge : public YSE::logHandler {
  public:
    void install(YseLogCallback cb, void* user_data) {
      // Publish user_data first; AddMessage gates on a non-null cb, so
      // any reader that observes the new cb is guaranteed to also see
      // the matching user_data via the acquire/release pair.
      this->user_data.store(user_data, std::memory_order_release);
      this->cb.store(cb, std::memory_order_release);
    }
    void AddMessage(const std::string& msg) override {
      auto fn = cb.load(std::memory_order_acquire);
      if (!fn) return;
      auto ud = user_data.load(std::memory_order_acquire);
      // Strings passed across NativeCallable.listener bridges to Dart
      // are marshalled by pointer value, not deep copy — by the time
      // the Dart handler runs, the std::string backing this pointer
      // has long been destroyed. Allocate a fresh malloc'd copy that
      // the receiver is contractually obliged to release via
      // yse_log_free_message.
      //
      // malloc on this path is acceptable today because log emits never
      // originate on the audio callback. If that ever changes, this
      // bridge needs a preallocated message pool — see issue #62.
      char* copy = static_cast<char*>(std::malloc(msg.size() + 1));
      if (!copy) return;
      std::memcpy(copy, msg.c_str(), msg.size());
      copy[msg.size()] = '\0';
      fn(copy, ud);
    }

  private:
    std::atomic<YseLogCallback> cb{nullptr};
    std::atomic<void*> user_data{nullptr};
  };

  CallbackBridge& bridge() {
    static CallbackBridge instance;
    return instance;
  }
} // namespace

extern "C" {

YSE_C_API YseLog* yse_log_get(void) {
  return reinterpret_cast<YseLog*>(&YSE::Log());
}

YSE_C_API void yse_log_send_message(YseLog* log, const char* msg) {
  if (log && msg) to_cpp(log)->sendMessage(msg);
}

YSE_C_API void yse_log_set_level(YseLog* log, YseErrorLevel level) {
  if (log) to_cpp(log)->setLevel(static_cast<YSE::ERROR_LEVEL>(level));
}

YSE_C_API YseErrorLevel yse_log_get_level(YseLog* log) {
  return log ? static_cast<YseErrorLevel>(to_cpp(log)->getLevel()) : YSE_EL_NONE;
}

YSE_C_API void yse_log_set_logfile(YseLog* log, const char* path) {
  if (log && path) to_cpp(log)->setLogfile(path);
}

YSE_C_API size_t yse_log_get_logfile(YseLog* log, char* buf, size_t cap) {
  if (!log) {
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
  }
  return copy_string(to_cpp(log)->getLogfile(), buf, cap);
}

YSE_C_API void yse_log_free_message(char* msg) {
  if (msg) std::free(msg);
}

YSE_C_API void yse_log_set_callback(YseLog* log, YseLogCallback cb, void* user_data) {
  if (!log) return;
  bridge().install(cb, user_data);
  // Installing the bridge unconditionally lets the user swap the callback
  // pointer freely without re-installing on the YSE side. Passing nullptr
  // for cb keeps the bridge installed but makes it a no-op; pass a real
  // callback to start receiving messages.
  to_cpp(log)->setHandler(cb ? &bridge() : nullptr);
}

} // extern "C"
