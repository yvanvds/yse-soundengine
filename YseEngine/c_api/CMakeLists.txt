# ── C ABI bridge (yse_*) ──────────────────────────────────────────────────────
# Folds extern "C" wrapper functions into yse_objects so libyse.dll exports
# both the C++ public API (mangled) and a flat C surface that language
# bindings (Dart FFI, Python ctypes, etc.) can call without needing C++ ABI
# compatibility.
#
# Included from YseEngine/CMakeLists.txt via include() AFTER yse_objects has
# been defined; never add_subdirectory()'d (the goal is to extend an existing
# target, not create a new one).
#
# Adding a new callback that the engine invokes on a non-host thread?
# Read the "Callback bridge rules" block at the top of yse_c_internal.hpp
# first. The short version: atomic-swap callback pointer (no mutex), no
# malloc on audio-callback-reachable paths, YSE_C_CALLBACK on the typedef.

set(YSE_C_API_SRCS
  yse_common.cpp
  yse_system.cpp
  yse_listener.cpp
  yse_channel.cpp
  yse_sound.cpp
  yse_reverb.cpp
  yse_device.cpp
  yse_dsp.cpp
  yse_dsp_modules.cpp
  yse_patcher.cpp
  yse_midi.cpp
  yse_music.cpp
  yse_log.cpp
  yse_buffer_io.cpp
  yse_enums_check.cpp
)
list(TRANSFORM YSE_C_API_SRCS PREPEND "${CMAKE_CURRENT_LIST_DIR}/")

target_sources(yse_objects PRIVATE ${YSE_C_API_SRCS})

# Public include root for the C headers: consumers of libyse get to write
#   #include "yse_c/yse_system.h"
# This is also what Dart's ffigen entry-point resolves against.
target_include_directories(yse_objects PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/include
)
