#include "yse_c/yse_common.h"
#include "yse_c_internal.hpp"

#include "../system.hpp"

#include <string>

namespace {
  thread_local std::string g_last_error;
}

namespace yse_c {

  // Assignment only throws on allocation failure; clear() never throws, so an
  // out-of-memory error report degrades to an empty message rather than a
  // second exception inside the caller's catch handler (issue #901).
  void set_last_error(const char* msg) noexcept {
    try {
      g_last_error = msg ? msg : "";
    } catch (...) {
      g_last_error.clear();
    }
  }

  void set_last_error(const std::string& msg) noexcept {
    try {
      g_last_error = msg;
    } catch (...) {
      g_last_error.clear();
    }
  }

  void set_unknown_exception(const char* where) noexcept {
    try {
      g_last_error = std::string(where ? where : "yse") + ": unknown C++ exception";
    } catch (...) {
      g_last_error.clear();
    }
  }

} // namespace yse_c

extern "C" {

YSE_C_API const char* yse_version(void) {
  return YSE::VERSION.c_str();
}

YSE_C_API const char* yse_last_error(void) {
  return g_last_error.c_str();
}

YSE_C_API void yse_clear_last_error(void) {
  g_last_error.clear();
}

} // extern "C"
