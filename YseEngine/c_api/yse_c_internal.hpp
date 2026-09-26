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
       is undefined behavior.

  When adding a new opaque type, follow the same shape so Dart's finaliser
  bookkeeping stays straight.

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

    1. Callback + user_data are published together: install builds an
       immutable {cb, user_data} node and swaps it into one std::atomic<>
       pointer; dispatch loads that pointer once and reads both fields from
       the same node. Never keep them as two separate atomics — a dispatch
       between the two stores of a re-install pairs one install's callback
       with another's user_data (issues #902, #916). No mutex, no
       lock_guard on the dispatch path.

       The replaced node may only be freed once no dispatch can still be
       reading it. Name the grace period that proves this beside the code:
       the log bridge frees after setHandler() has taken the sink mutex that
       every dispatch runs under (yse_log.cpp); the MIDI raw bridge, whose
       input thread holds no such lock, uses a seq_cst reader-count
       handshake around the pointer load (yse_midi.cpp). Keep user code out
       of the window the installer waits on.

       Passing cb + user_data straight through to an engine setter is only
       safe when that setter keeps them as one pair itself — YSE::midiIn's
       raw / parsed setters do since #917 (the parsed C callback relies on
       it). Wrap such an install in the exception barrier: it allocates.

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

#include <cstddef>
#include <exception>
#include <string>
#include <utility>

// Forward declarations for the cross-TU synth-handle accessor below. Kept
// minimal so this header stays free of engine includes; the definitions live
// in yse_synth.cpp (YseSynthImpl) and the engine synth headers.
struct YseSynth;
struct YseDspBuffer;
struct YseMidiIn;
struct YsePatcher;
namespace YSE {
  class patcher;
  namespace SYNTH {
    class interfaceObject;
  }
  typedef SYNTH::interfaceObject synth;
  class midiIn;
  namespace DSP {
    class buffer;
  }
} // namespace YSE

namespace yse_c {

  // Stash a human-readable error in the thread-local last_error slot.
  // Retrieved by the C client via yse_last_error(). noexcept because both are
  // called from inside catch handlers at the ABI boundary: if storing the
  // message itself runs out of memory, the slot is cleared instead of a second
  // exception escaping the extern "C" function (issue #901).
  void set_last_error(const char* msg) noexcept;
  void set_last_error(const std::string& msg) noexcept;

  // Record "<where>: unknown C++ exception" for a catch (...) handler, without
  // letting the string concatenation throw.
  void set_unknown_exception(const char* where) noexcept;

  // ─── Exception barrier (issue #901) ─────────────────────────────────────
  // An exception escaping an extern "C" function is undefined behaviour and in
  // practice terminates the host. Every entry point that reaches C++ which can
  // throw — anything that builds a std::string, re-parses, or allocates — runs
  // its body through one of these: std::exception and anything else are both
  // caught, reported through yse_last_error(), and turned into the function's
  // documented failure value.

  // Value-returning body: returns `body()`, or `fallback` if it threw.
  template <typename R, typename F> R guard(const char* where, R fallback, F&& body) noexcept {
    try {
      return std::forward<F>(body)();
    } catch (const std::exception& e) {
      set_last_error(e.what());
    } catch (...) {
      set_unknown_exception(where);
    }
    return fallback;
  }

  // Void body: a state change that failed is reported and otherwise a no-op.
  template <typename F> void guard_void(const char* where, F&& body) noexcept {
    try {
      std::forward<F>(body)();
    } catch (const std::exception& e) {
      set_last_error(e.what());
    } catch (...) {
      set_unknown_exception(where);
    }
  }

  // snprintf-style string getter: `body()` returns the full length it copied
  // into buf. On a throw the caller's buffer is cleared and 0 is returned — the
  // same answer as a NULL handle — so a host never reads a half-written buffer.
  template <typename F>
  std::size_t guard_string(const char* where, char* buf, std::size_t cap, F&& body) noexcept {
    try {
      return std::forward<F>(body)();
    } catch (const std::exception& e) {
      set_last_error(e.what());
    } catch (...) {
      set_unknown_exception(where);
    }
    if (buf != nullptr && cap > 0) buf[0] = '\0';
    return 0;
  }

  // Return the engine synth backing a YseSynth handle, or nullptr for a NULL
  // handle. Defined in yse_synth.cpp — lets yse_music.cpp's player create()
  // reach the synth a player must drive without exposing YseSynthImpl's private
  // layout across translation units (issue #268).
  YSE::synth* synth_from_handle(YseSynth* h);

  // Return the engine buffer backing a YseDspBuffer handle, or nullptr for a
  // NULL handle. Defined in yse_dsp.cpp — a YseDspBuffer* is *not* a
  // DSP::buffer* since issue #662: the handle owns its buffer through a
  // polymorphic wrapper so destroy can run the most-derived destructor of the
  // non-polymorphic buffer <- drawableBuffer <- fileBuffer <- wavetable chain.
  // Any TU that needs the engine object must go through here, never through a
  // reinterpret_cast of the handle.
  YSE::DSP::buffer* buffer_from_handle(YseDspBuffer* h);

  // Return the engine input port backing a YseMidiIn handle, or nullptr for a
  // NULL handle. Defined in yse_midi.cpp, and only on builds with MIDI device
  // support (YSE_ENABLE_MIDI_DEVICE). A YseMidiIn* is not a midiIn* — the
  // handle wraps the port together with the raw-callback bridge state. Tests
  // use this to drive the port's dispatch path, and so the C bridge, without
  // a hardware device (issue #916).
  YSE::midiIn* midi_in_from_handle(YseMidiIn* h);

  // Return the engine patcher backing a YsePatcher handle, or nullptr for a
  // NULL handle. Defined in yse_patcher.cpp. A YsePatcher* is not a
  // YSE::patcher* since issue #907: the handle owns the patcher together with
  // its send-callback bridge, so every TU that needs the engine object goes
  // through here, never through a reinterpret_cast of the handle.
  YSE::patcher* patcher_from_handle(YsePatcher* h);

} // namespace yse_c
