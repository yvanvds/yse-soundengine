/*
  yse_c_internal.hpp — private helpers shared across the c_api translation units.
  Not installed; never included by Dart or any C ABI consumer.

  Handle ownership convention: every opaque Yse* typedef in the public
  headers carries a one-line ownership comment of one of these shapes:

    - "Owned — release with yse_<module>_destroy."
      Pair every yse_<module>_create with the corresponding destroy. The
      C API never frees these for you.

    - "Borrowed — owned by <source>, never destroy."
      Singletons (yse_system_get, yse_listener_get, yse_log_get), pre-built
      channels, device descriptors enumerated from the engine, and pHandles
      that belong to a parent YsePatcher. Calling the home destroy on these
      is undefined behaviour.

  When adding a new opaque type, follow the same shape so Dart's finaliser
  bookkeeping stays straight.
*/

#pragma once

#include <string>

namespace yse_c {

// Stash a human-readable error in the thread-local last_error slot.
// Retrieved by the C client via yse_last_error().
void set_last_error(const char* msg);
void set_last_error(const std::string& msg);

} // namespace yse_c
