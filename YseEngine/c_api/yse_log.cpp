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
  // a new callback replaces the slot. See issues #58 and #902.
  //
  // ### One pair, one pointer (issue #902)
  //
  // cb and user_data used to be two separate atomics, stored one after the
  // other. A message dispatched between the two stores of a re-install could
  // load the old cb with the new user_data (or, by the load order, the new
  // user_data with the old cb) — a callback handed a pointer that belongs to
  // somebody else. Each install now builds an immutable Pair and publishes it
  // with one atomic exchange, so a dispatch sees either the whole old pair or
  // the whole new one.
  //
  // ### Reclaiming the old pair
  //
  // The engine only ever calls AddMessage() from logImplementation::logMessage()
  // with the sink's mutex held (#820), and yse_log_set_callback() follows every
  // install with Log().setHandler(), which takes that same mutex. Once
  // setHandler() has acquired it, every dispatch that could have loaded the
  // old pair has finished, and every later one synchronises with our exchange
  // and loads the new pair — so the installer frees the pair it swapped out
  // right after setHandler() returns. That grace period is the contract
  // retire() documents; nothing here takes a lock of its own.
  //
  // ### The malloc per message
  //
  // Re-checked under #820's multi-thread logging: the copy below is allocated
  // on whichever engine thread emitted the line, never on the audio callback.
  // The bridge is reachable only through logImplementation, which has already
  // built a std::string for the line and taken a mutex before calling in — so
  // a thread that may not allocate cannot get here without having broken the
  // rule upstream of the bridge. Render-thread producers go through
  // internal/rtLogQueue instead, whose drain() runs on the control thread
  // (#546). The bridge's own allocation adds no new constraint.
  class CallbackBridge : public YSE::logHandler {
  public:
    struct Pair {
      YseLogCallback cb;
      void* user_data;
    };

    // Publishes (cb, user_data) as one unit and returns the pair it replaced
    // (possibly nullptr). The caller must pass that pair to retire() once no
    // dispatch can still be reading it — see the class notes. Allocates, so it
    // may throw std::bad_alloc; the caller runs it inside the ABI guard.
    Pair* install(YseLogCallback cb, void* user_data) {
      Pair* next = cb != nullptr ? new Pair{cb, user_data} : nullptr;
      return current.exchange(next, std::memory_order_acq_rel);
    }

    // Frees a pair returned by install(). Only call it after the log sink's
    // mutex has been taken and released since the install — which
    // Log().setHandler() does.
    static void retire(Pair* old) noexcept {
      delete old;
    }

    void AddMessage(const std::string& msg) override {
      const Pair* pair = current.load(std::memory_order_acquire);
      if (pair == nullptr) return;
      // Strings passed across NativeCallable.listener bridges to Dart
      // are marshalled by pointer value, not deep copy — by the time
      // the Dart handler runs, the std::string backing this pointer
      // has long been destroyed. Allocate a fresh malloc'd copy that
      // the receiver is contractually obliged to release via
      // yse_log_free_message.
      char* copy = static_cast<char*>(std::malloc(msg.size() + 1));
      if (!copy) return;
      std::memcpy(copy, msg.c_str(), msg.size());
      copy[msg.size()] = '\0';
      pair->cb(copy, pair->user_data);
    }

    // The last installed pair is deliberately not freed here: this bridge is
    // a function-local static whose destruction order relative to the log
    // implementation is unspecified, and a line logged during static teardown
    // may still dispatch through it. The pointer stays reachable from static
    // storage, so leak checkers do not report it.

  private:
    std::atomic<Pair*> current{nullptr};
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
  if (!log || !msg) return;
  yse_c::guard_void("yse_log_send_message", [&] { to_cpp(log)->sendMessage(msg); });
}

YSE_C_API void yse_log_set_level(YseLog* log, YseErrorLevel level) {
  if (log) to_cpp(log)->setLevel(static_cast<YSE::ERROR_LEVEL>(level));
}

YSE_C_API YseErrorLevel yse_log_get_level(YseLog* log) {
  return log ? static_cast<YseErrorLevel>(to_cpp(log)->getLevel()) : YSE_EL_NONE;
}

YSE_C_API void yse_log_set_logfile(YseLog* log, const char* path) {
  if (!log || !path) return;
  yse_c::guard_void("yse_log_set_logfile", [&] { to_cpp(log)->setLogfile(path); });
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
  yse_c::guard_void("yse_log_set_callback", [&] {
    CallbackBridge::Pair* old = bridge().install(cb, user_data);
    // Passing nullptr for cb detaches the bridge and restores the default file
    // sink; a real callback routes every line through the bridge. Either way
    // setHandler() takes the sink's mutex, which is the grace period that makes
    // freeing the replaced pair below safe (issue #902).
    to_cpp(log)->setHandler(cb ? &bridge() : nullptr);
    CallbackBridge::retire(old);
  });
}

} // extern "C"
