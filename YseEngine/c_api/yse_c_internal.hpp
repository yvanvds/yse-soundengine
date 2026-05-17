/*
  yse_c_internal.hpp — private helpers shared across the c_api translation units.
  Not installed; never included by Dart or any C ABI consumer.
*/

#pragma once

#include <string>

namespace yse_c {

// Stash a human-readable error in the thread-local last_error slot.
// Retrieved by the C client via yse_last_error().
void set_last_error(const char* msg);
void set_last_error(const std::string& msg);

} // namespace yse_c
