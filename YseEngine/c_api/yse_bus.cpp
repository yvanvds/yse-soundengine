/*
  yse_bus.cpp — host tap onto the global named bus (issue #389), plus host
  publish and exact-address subscribe (issue #904).

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
#include <memory>
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

  struct YseBusSubImpl {
    YSE::INTERNAL::SubHandle handle = 0;
  };

  inline YseBusSubImpl* to_impl(YseBusSub* h) {
    return reinterpret_cast<YseBusSubImpl*>(h);
  }

  // Unpack one BusValue into the flat (address, kind, payload…) frame shared
  // by yse_bus_tap_cb and yse_bus_sub_cb. Everything handed to the host is
  // engine-owned and valid only for this call — the header tells the host to
  // copy before returning.
  template <typename Cb>
  void emitFrame(Cb cb, const char* address, const BusValue& value, void* user_data) {
    if (const int* ip = std::get_if<int>(&value)) {
      cb(address, YSE_BUS_INT, *ip, 0.0f, nullptr, nullptr, 0, user_data);
    } else if (const float* fp = std::get_if<float>(&value)) {
      cb(address, YSE_BUS_FLOAT, 0, *fp, nullptr, nullptr, 0, user_data);
    } else if (const std::string* sp = std::get_if<std::string>(&value)) {
      cb(address, YSE_BUS_STRING, 0, 0.0f, sp->c_str(), nullptr, 0, user_data);
    } else if (const std::vector<float>* lp = std::get_if<std::vector<float>>(&value)) {
      cb(address, YSE_BUS_LIST, 0, 0.0f, nullptr, lp->data(), lp->size(), user_data);
    } else {
      // monostate — a bang (e.g. patcher gSend bang outlet).
      cb(address, YSE_BUS_BANG, 0, 0.0f, nullptr, nullptr, 0, user_data);
    }
  }

  // Shared precondition check for the publish family. Returns YSE_OK when the
  // publish may proceed; otherwise sets the last error and returns the code.
  YseStatus checkPublish(const char* fn, const char* address) {
    if (address == nullptr || address[0] == '\0') {
      yse_c::set_last_error(std::string(fn) + ": address must be a non-empty string");
      return YSE_ERR_INVALID_ARGUMENT;
    }
    if (!YSE::INTERNAL::Global().isActive()) {
      yse_c::set_last_error(std::string(fn) + ": engine not initialised — publish after "
                                              "yse_system_init / yse_system_init_offline");
      return YSE_ERR_NOT_INITIALIZED;
    }
    return YSE_OK;
  }

  // Publish as a control-rate producer (T_GUI), the same attribution a
  // script's yse.send() uses: inline dispatch on the control thread, the
  // control inbox from any other thread. T_DSP is deliberately not used — it
  // would claim one of the bus's per-render-thread SPSC queue slots for every
  // host thread that ever publishes, and it drops strings / lists / bangs.
  YseStatus publishValue(const char* fn, const char* address, BusValue value) {
    try {
      YSE::INTERNAL::Bus().publish(address, value, YSE::T_GUI);
      return YSE_OK;
    } catch (const std::exception& e) {
      yse_c::set_last_error(std::string(fn) + ": " + e.what());
      return YSE_ERR_EXCEPTION;
    } catch (...) {
      yse_c::set_last_error(std::string(fn) + ": unknown C++ exception");
      return YSE_ERR_EXCEPTION;
    }
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
          emitFrame(cb, name.c_str(), value, user_data);
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

YSE_C_API YseStatus yse_bus_publish_bang(const char* address) {
  const YseStatus st = checkPublish("yse_bus_publish_bang", address);
  if (st != YSE_OK) return st;
  return publishValue("yse_bus_publish_bang", address, BusValue{});
}

YSE_C_API YseStatus yse_bus_publish_int(const char* address, int value) {
  const YseStatus st = checkPublish("yse_bus_publish_int", address);
  if (st != YSE_OK) return st;
  return publishValue("yse_bus_publish_int", address, BusValue{value});
}

YSE_C_API YseStatus yse_bus_publish_float(const char* address, float value) {
  const YseStatus st = checkPublish("yse_bus_publish_float", address);
  if (st != YSE_OK) return st;
  return publishValue("yse_bus_publish_float", address, BusValue{value});
}

YSE_C_API YseStatus yse_bus_publish_string(const char* address, const char* value) {
  if (value == nullptr) {
    yse_c::set_last_error("yse_bus_publish_string: value must be non-NULL");
    return YSE_ERR_INVALID_ARGUMENT;
  }
  const YseStatus st = checkPublish("yse_bus_publish_string", address);
  if (st != YSE_OK) return st;
  try {
    return publishValue("yse_bus_publish_string", address, BusValue{std::string(value)});
  } catch (...) { // std::string construction (bad_alloc)
    yse_c::set_last_error("yse_bus_publish_string: failed to copy the value");
    return YSE_ERR_EXCEPTION;
  }
}

YSE_C_API YseStatus yse_bus_publish_list(const char* address, const float* values, size_t count) {
  if (values == nullptr && count != 0) {
    yse_c::set_last_error("yse_bus_publish_list: values is NULL but count is non-zero");
    return YSE_ERR_INVALID_ARGUMENT;
  }
  const YseStatus st = checkPublish("yse_bus_publish_list", address);
  if (st != YSE_OK) return st;
  try {
    std::vector<float> list;
    if (count != 0) list.assign(values, values + count);
    return publishValue("yse_bus_publish_list", address, BusValue{std::move(list)});
  } catch (...) { // vector copy (bad_alloc / length_error)
    yse_c::set_last_error("yse_bus_publish_list: failed to copy the values");
    return YSE_ERR_EXCEPTION;
  }
}

YSE_C_API YseBusSub* yse_bus_subscribe(const char* address, yse_bus_sub_cb cb, void* user_data) {
  if (address == nullptr || address[0] == '\0' || cb == nullptr) {
    yse_c::set_last_error("yse_bus_subscribe: address must be a non-empty string and cb non-NULL");
    return nullptr;
  }
  if (!YSE::INTERNAL::Global().isActive()) {
    yse_c::set_last_error("yse_bus_subscribe: engine not initialised — subscribe after "
                          "yse_system_init / yse_system_init_offline");
    return nullptr;
  }
  try {
    auto impl = std::make_unique<YseBusSubImpl>();
    // The closure captures the address, callback and user_data BY VALUE and
    // never touches the YseBusSub handle — same no-mutable-state argument as
    // the tap above. Dispatch runs on the control thread only.
    std::string name(address);
    impl->handle =
        YSE::INTERNAL::Bus().subscribe(name, [cb, user_data, name](const BusValue& value) {
          emitFrame(cb, name.c_str(), value, user_data);
        });
    return reinterpret_cast<YseBusSub*>(impl.release());
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("yse_bus_subscribe: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API void yse_bus_unsubscribe(YseBusSub* sub) {
  if (sub == nullptr) return;
  YseBusSubImpl* impl = to_impl(sub);
  // Same close/re-init reasoning as yse_bus_tap_destroy: subscription handles
  // are process-unique (issue #716), so a stale unsubscribe after a re-init
  // is a guaranteed no-op on the new bus.
  if (YSE::INTERNAL::Global().isActive()) {
    YSE::INTERNAL::Bus().unsubscribe(impl->handle);
  }
  delete impl;
}

} // extern "C"
