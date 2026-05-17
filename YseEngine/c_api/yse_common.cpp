#include "yse_c/yse_common.h"
#include "yse_c_internal.hpp"

#include "../system.hpp"

#include <string>

namespace {
  thread_local std::string g_last_error;
}

namespace yse_c {

void set_last_error(const char* msg) {
  g_last_error = msg ? msg : "";
}

void set_last_error(const std::string& msg) {
  g_last_error = msg;
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
