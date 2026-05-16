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
option(YSE_BUILD_BENCHMARKS
  "Build the Bench/ benchmark suite (fetches google-benchmark via FetchContent)"
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

# RtMidi is required on every desktop platform we build for here (Windows and
# Linux).  The MIDI device source files (device.cpp, midiDeviceManager.cpp)
# need RtMidi symbols; they are guarded by `#if YSE_WINDOWS || YSE_LINUX` so
# they remain compiled-out on Android, but the CMake build does not target
# Android, so the dependency is unconditionally required here.
pkg_check_modules(RtMidi REQUIRED IMPORTED_TARGET rtmidi)
message(STATUS "RtMidi found — MIDI device backend enabled")

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

# ── LLVM source-based coverage (Windows / MSYS2 Clang64) ─────────────────────
# Uses -fprofile-instr-generate / -fcoverage-mapping instead of --coverage so
# that llvm-profdata + llvm-cov can produce the JSON report consumed by
# sonar.cfamily.llvm-cov.reportPath.  Mutually exclusive with YSE_ENABLE_COVERAGE.
option(YSE_LLVM_COVERAGE
  "Enable LLVM source-based coverage instrumentation (Windows/Clang; use YSE_ENABLE_COVERAGE on Linux)"
  OFF)
if(YSE_LLVM_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "YSE_LLVM_COVERAGE requires Clang")
  endif()
  set(YSE_BUILD_TESTS ON CACHE BOOL "Forced ON by YSE_LLVM_COVERAGE" FORCE)
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
  add_link_options(-fprofile-instr-generate)
  message(STATUS "LLVM source-based coverage enabled (-fprofile-instr-generate)")
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

# ── Benchmarks ────────────────────────────────────────────────────────────────
# Google Benchmark is fetched on demand so the default build has zero extra
# dependencies.  Pinned to a tagged release; bump explicitly when needed so the
# version is reproducible across CI runs and developer machines.
if(YSE_BUILD_BENCHMARKS)
  include(FetchContent)
  set(BENCHMARK_ENABLE_TESTING     OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL     OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_INSTALL_DOCS       OFF CACHE BOOL "" FORCE)
  # Upstream's own build defaults to -Werror, which fails on newer Clang
  # versions that warn about __COUNTER__ as a C2y extension (the same
  # macro doctest uses; see Tests/CMakeLists.txt). It's a vendored
  # dependency we're not modifying, so don't gate our build on its
  # warning hygiene.
  set(BENCHMARK_ENABLE_WERROR      OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.0
  )
  FetchContent_MakeAvailable(benchmark)
  # The engine's global -Wall -Wextra -Wpedantic propagates into upstream's
  # translation units; silence the two diagnostics that fire there
  # (-Wc2y-extensions on __COUNTER__, -Wswitch on a missing enum branch in
  # sysinfo.cc). Scoped to the upstream targets so engine code is unaffected.
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(benchmark      PRIVATE -Wno-c2y-extensions -Wno-switch)
    target_compile_options(benchmark_main PRIVATE -Wno-c2y-extensions -Wno-switch)
  endif()
  add_subdirectory(Bench)
endif()
