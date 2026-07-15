/*
  yse_python.cpp — C ABI scripting surface (issue #125, epic #119).

  The host-facing callback (cb + user_data) lives here as atomics, following
  the callback-bridge convention in yse_c_internal.hpp: the host installs with
  release ordering, and the engine reads with acquire ordering when it drains a
  traceback. A single fixed bridge function (c_script_error_bridge) is handed to
  the engine as the error sink; it reads these atomics on each call, so swapping
  the callback never leaves the bridge observing a half-installed pair and the
  previous callback never receives a later traceback.

  Unlike scriptRuntime.cpp (compiled only when YSE_ENABLE_PYTHON=ON), this TU is
  always built so the C ABI surface is uniform: the three symbols exist with
  identical signatures in both configurations, and only the yse_run_script /
  yse_python_enabled bodies branch on the macro.
*/

#include "yse_c/yse_python.h"

#include "../internal/global.h"

#include <atomic>
#include <string>

namespace {

  // Host-facing callback + user_data. user_data is published before the
  // function pointer (release) and read after it (acquire); see the bridge.
  std::atomic<yse_script_error_cb> g_userCb{nullptr};
  std::atomic<void*> g_userData{nullptr};

  // Fixed sink handed to the engine. The `userdata` slot from the engine side
  // is unused — the host callback and its user_data are read from the atomics
  // above so a mid-dispatch swap takes effect on the next traceback.
  void c_script_error_bridge(const char* traceback, void* /*engine_userdata*/) {
    auto cb = g_userCb.load(std::memory_order_acquire);
    if (cb == nullptr) return;
    auto user = g_userData.load(std::memory_order_acquire);
    cb(traceback, user);
  }

} // namespace

extern "C" {

YSE_C_API int yse_python_enabled(void) {
#if YSE_ENABLE_PYTHON
  return 1;
#else
  return 0;
#endif
}

YSE_C_API void yse_run_script(const char* src) {
#if YSE_ENABLE_PYTHON
  if (src == nullptr) return;
  YSE::INTERNAL::Global().pushScript(std::string(src));
#else
  // No interpreter to run on: report the misconfiguration synchronously through
  // the same callback path the ON build delivers tracebacks on.
  (void)src;
  auto cb = g_userCb.load(std::memory_order_acquire);
  if (cb == nullptr) return;
  auto user = g_userData.load(std::memory_order_acquire);
  cb("YSE compiled without YSE_ENABLE_PYTHON", user);
#endif
}

YSE_C_API void yse_set_script_error_callback(yse_script_error_cb cb, void* userdata) {
  // Publish user_data before the function pointer, both release — pairs with the
  // acquire loads in the bridge.
  g_userData.store(userdata, std::memory_order_release);
  g_userCb.store(cb, std::memory_order_release);
  // Keep the engine sink wired to our bridge. Re-registering is a cheap atomic
  // store; the bridge reads the host callback fresh each time, so this stays
  // correct whether cb is being set or cleared. On an OFF build this is a no-op
  // store (the engine never drains results) but keeps the code path uniform.
  YSE::INTERNAL::Global().setScriptErrorSink(&c_script_error_bridge, nullptr);
}

} // extern "C"
