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

  ─── Callback bridge rules ───────────────────────────────────────────────────

  Several C API entry points install user-provided C function pointers that
  the engine then invokes from a non-host thread (audio callback, RtMidi
  input thread, future occlusion / dspSourceObject / customFileReader paths).
  These bridges sit on RT-sensitive paths even when they don't realise it.

  The canonical pattern lives in yse_midi.cpp's c_raw_bridge. Any new bridge
  must follow the same rules:

    1. Callback + user_data live as std::atomic<>. Install stores both with
       release ordering (user_data first, then cb); dispatch loads with
       acquire ordering (cb first, returns if null, then user_data). No
       mutex, no lock_guard.

    2. No malloc / new / std::string / container ops on the dispatch path
       when the bridge can fire from the audio callback. For occlusion and
       dspSourceObject, pass values by stack copy. For raw byte buffers
       (file reader, MIDI raw), preallocate a per-handle pool — do not
       malloc per call.

       The current log + MIDI raw bridges allocate a malloc'd copy because
       Dart's NativeCallable.listener marshals by pointer value (the
       std::string / std::vector backing dies before the Dart handler
       runs). That's acceptable today because neither path lives on the
       audio callback. If an audio-callback emit is ever wired up, those
       bridges must switch to a preallocated pool.

    3. Public callback typedefs in the include/yse_c headers use the
       YSE_C_CALLBACK macro (from yse_common.h) so the calling convention
       is unambiguous across compilers — __cdecl on Win32, no-op elsewhere.

    4. When the bridge transfers ownership of a heap buffer to the host
       (e.g. MIDI raw bytes, log strings), pair the callback with a
       yse_<module>_free_message function so the host can release it.
       Document the contract in the header next to the typedef.

    5. Never throw across the C ABI boundary. Wrap any C++ surface that
       can throw in try/catch and translate to YseStatus + set_last_error.
*/

#pragma once

#include <string>

namespace yse_c {

// Stash a human-readable error in the thread-local last_error slot.
// Retrieved by the C client via yse_last_error().
void set_last_error(const char* msg);
void set_last_error(const std::string& msg);

} // namespace yse_c
