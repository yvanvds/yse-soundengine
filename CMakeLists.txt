cmake_minimum_required(VERSION 3.20)
project(yse-soundengine LANGUAGES CXX)

# ── Default build type ────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ── Output layout ─────────────────────────────────────────────────────────────
# All executables and shared libraries go to build/bin/.
# Demo hard-coded paths ("../../TestResources/...") resolve correctly when
# demos are run from that directory:  cd build/bin && ./Demo00
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ── Options ───────────────────────────────────────────────────────────────────
option(YSE_ENABLE_LTO
  "Enable link-time optimization for Release builds"
  OFF)
option(YSE_NATIVE_ARCH
  "Add -march=native (local dev builds only — do NOT use for distributable binaries)"
  OFF)
option(YSE_BUILD_TESTS
  "Build the Tests/ unit-test suite (requires doctest vendored under dependencies/doctest/)"
  OFF)
option(YSE_ENABLE_COVERAGE
  "Instrument the build for code coverage — implies YSE_BUILD_TESTS; Linux/GCC or Clang only"
  OFF)

# Export compile_commands.json for SonarCloud's C++ analyser and clangd.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(PortAudio REQUIRED IMPORTED_TARGET portaudio-2.0)
pkg_check_modules(SndFile   REQUIRED IMPORTED_TARGET sndfile)

# RtMidi is required on Windows: the MIDI device source files (device.cpp,
# midiDeviceManager.cpp) only compile on Windows and need RtMidi symbols.
# On Linux those files are guarded by #if YSE_WINDOWS and compile to empty
# TUs, so RtMidi is optional there.
if(WIN32)
  pkg_check_modules(RtMidi REQUIRED IMPORTED_TARGET rtmidi)
else()
  pkg_check_modules(RtMidi IMPORTED_TARGET rtmidi)
endif()

if(RtMidi_FOUND)
  message(STATUS "RtMidi found — Demo16 and Demo17 (MIDI) will be built")
else()
  message(STATUS "RtMidi not found — Demo16 and Demo17 (MIDI) will be skipped")
endif()

# ── Compiler flags ────────────────────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    # -O3 for Release. Explicitly no -ffast-math: it breaks IEEE 754 float
    # semantics in ways that produce subtle DSP bugs (e.g. NaN propagation,
    # denormal flushing). -fno-math-errno is a safe subset: the engine never
    # reads errno after math calls.
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-fno-math-errno>
  )
  # NDEBUG disables asserts in Release without -ffast-math side effects.
  add_compile_definitions($<$<CONFIG:Release>:NDEBUG>)

  if(YSE_NATIVE_ARCH)
    add_compile_options(-march=native)
    message(WARNING
      "YSE_NATIVE_ARCH is ON — resulting binaries will not run on other machines")
  endif()

  # Linux: embed $ORIGIN rpath so demos find libyse.so in the same directory.
  if(UNIX AND NOT APPLE)
    set(CMAKE_BUILD_RPATH "$ORIGIN")
  endif()
endif()

# ── Coverage ──────────────────────────────────────────────────────────────────
# When YSE_ENABLE_COVERAGE=ON every target in this build (library + tests) is
# instrumented so that the SonarCloud coverage report reflects library code
# exercised by the tests, not just the test harness itself.  The option is OFF
# by default so normal demo / Android builds are completely unaffected.
if(YSE_ENABLE_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "YSE_ENABLE_COVERAGE requires GCC or Clang")
  endif()
  set(YSE_BUILD_TESTS ON CACHE BOOL "Forced ON by YSE_ENABLE_COVERAGE" FORCE)
  add_compile_options(--coverage)
  add_link_options(--coverage)
  message(STATUS "Code coverage instrumentation enabled (--coverage)")
endif()

# ── LTO ───────────────────────────────────────────────────────────────────────
if(YSE_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _lto_ok OUTPUT _lto_err)
  if(_lto_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    message(STATUS "LTO enabled for Release builds")
  else()
    message(WARNING "YSE_ENABLE_LTO is ON but LTO is not supported: ${_lto_err}")
  endif()
endif()

# ── Subdirectories ────────────────────────────────────────────────────────────
add_subdirectory(YseEngine)

# Demos are Windows-only (the directory name says as much; they use Windows
# console APIs and depend on RtMidi for MIDI demos).  Skip on Linux/CI.
if(WIN32)
  add_subdirectory(Demo.Windows.Native)
endif()

if(YSE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(Tests)
endif()
