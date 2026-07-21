/*
  yse_bus.cpp — host tap onto the global named bus (issue #389).

  Each tap is an engine-side prefix subscription (INTERNAL::NamedBus::
  subscribeTap) whose closure captures the host callback + user_data BY VALUE.
  Nothing is mutable after create — there is no swap API, so the atomic-swap
  install discipline from yse_c_internal.hpp's callback-bridge rules does not
  apply here; it governs mutable global callback slots (yse_python.cpp). The
  closure never dereferences the YseBusTap handle either, so a copied-out
  dispatch cannot use-after-free a destroyed tap.

  Taps fire from NamedBus::dispatch(), which only ever runs on the control
  thread (the thread driving yse_system_update(), or a control-thread publish
  dispatching inline). The audio-thread publish path is untouched, so the
  no-allocation rule for audio-callback-reachable bridges does not bind —
  the variant unpacking below may touch heap-backed strings freely.

  Lifecycle: NamedBus dies at System::close(), taking every engine-side tap
  registration with it. yse_bus_tap_destroy therefore only touches the bus
  while a session is active; tap handles are process-unique across sessions
  (namedBus.cpp), so a stale destroy after a re-init is a guaranteed no-op on
  the new bus.
*/

#include "yse_c/yse_bus.h"

#include "yse_c_internal.hpp"

#include "../internal/global.h"
#include "../internal/namedBus.h"

#include <exception>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

  // Drift guard: YseBusValueKind mirrors the alternative order of the
  // engine's BusValue variant. There is no engine enum to YSE_ASSERT_ENUM
  // against, so pin the indices here — reordering the variant (or the C enum)
  // breaks the build loudly instead of silently mislabeling payloads.
  using YSE::INTERNAL::BusValue;
  static_assert(std::variant_size_v<BusValue> == 5,
                "BusValue gained/lost an alternative — update YseBusValueKind and this bridge");
  static_assert(std::is_same_v<std::variant_alternative_t<YSE_BUS_BANG, BusValue>, std::monostate>,
                "YSE_BUS_BANG must index BusValue's monostate alternative");
  static_assert(std::is_same_v<std::variant_alternative_t<YSE_BUS_INT, BusValue>, int>,
                "YSE_BUS_INT must index BusValue's int alternative");
  static_assert(std::is_same_v<std::variant_alternative_t<YSE_BUS_FLOAT, BusValue>, float>,
                "YSE_BUS_FLOAT must index BusValue's float alternative");
  static_assert(std::is_same_v<std::variant_alternative_t<YSE_BUS_STRING, BusValue>, std::string>,
                "YSE_BUS_STRING must index BusValue's string alternative");
  static_assert(
      std::is_same_v<std::variant_alternative_t<YSE_BUS_LIST, BusValue>, std::vector<float>>,
      "YSE_BUS_LIST must index BusValue's list alternative");

  struct YseBusTapImpl {
    YSE::INTERNAL::TapHandle handle = 0;
  };

  inline YseBusTapImpl* to_impl(YseBusTap* h) {
    return reinterpret_cast<YseBusTapImpl*>(h);
  }

} // namespace

extern "C" {

YSE_C_API YseBusTap* yse_bus_tap_create(const char* prefix, yse_bus_tap_cb cb, void* user_data) {
  if (prefix == nullptr || cb == nullptr) {
    yse_c::set_last_error("yse_bus_tap_create: prefix and cb must be non-NULL");
    return nullptr;
  }
  if (!YSE::INTERNAL::Global().isActive()) {
    yse_c::set_last_error("yse_bus_tap_create: engine not initialised — create taps after "
                          "yse_system_init / yse_system_init_offline");
    return nullptr;
  }
  try {
    auto* impl = new YseBusTapImpl;
    impl->handle = YSE::INTERNAL::Bus().subscribeTap(
        prefix, [cb, user_data](const std::string& name, const BusValue& value) {
          // Everything handed to the host is engine-owned and valid only for
          // this call — the header tells the host to copy before returning.
          if (const int* ip = std::get_if<int>(&value)) {
            cb(name.c_str(), YSE_BUS_INT, *ip, 0.0f, nullptr, nullptr, 0, user_data);
          } else if (const float* fp = std::get_if<float>(&value)) {
            cb(name.c_str(), YSE_BUS_FLOAT, 0, *fp, nullptr, nullptr, 0, user_data);
          } else if (const std::string* sp = std::get_if<std::string>(&value)) {
            cb(name.c_str(), YSE_BUS_STRING, 0, 0.0f, sp->c_str(), nullptr, 0, user_data);
          } else if (const std::vector<float>* lp = std::get_if<std::vector<float>>(&value)) {
            cb(name.c_str(), YSE_BUS_LIST, 0, 0.0f, nullptr, lp->data(), lp->size(), user_data);
          } else {
            // monostate — a bang (e.g. patcher gSend bang outlet).
            cb(name.c_str(), YSE_BUS_BANG, 0, 0.0f, nullptr, nullptr, 0, user_data);
          }
        });
    return reinterpret_cast<YseBusTap*>(impl);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("yse_bus_tap_create: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API void yse_bus_tap_destroy(YseBusTap* tap) {
  if (tap == nullptr) return;
  YseBusTapImpl* impl = to_impl(tap);
  // Only touch the bus while a session is active: yse_system_close() already
  // tore the engine-side registration down with the bus. Handles are
  // process-unique, so after a close + re-init this unsubscribe of a stale
  // handle is a safe no-op on the new bus.
  if (YSE::INTERNAL::Global().isActive()) {
    YSE::INTERNAL::Bus().unsubscribeTap(impl->handle);
  }
  delete impl;
}

} // extern "C"
