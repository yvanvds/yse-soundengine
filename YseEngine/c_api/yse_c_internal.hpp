/*
  yse_c_internal.hpp — private helpers shared across the c_api translation units.
  Not installed; never included by Dart or any C ABI consumer.

  C API conventions (apply to all new functions):

    - Void-returning state-change functions (play / pause / stop, setters,
      etc.) are null-safe no-ops when called with a NULL handle. Status
      queries that return int / float / pointer return zero / false / NULL
      on NULL handles.
    - Functions that can fail meaningfully (file load, create, parse) use
      YseStatus and populate yse_last_error() on failure.
    - Document any deviation from these defaults explicitly in the header.
*/

#pragma once

#include <string>

namespace yse_c {

// Stash a human-readable error in the thread-local last_error slot.
// Retrieved by the C client via yse_last_error().
void set_last_error(const char* msg);
void set_last_error(const std::string& msg);

} // namespace yse_c
