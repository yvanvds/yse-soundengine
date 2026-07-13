cmake_minimum_required(VERSION 3.20)
# C is needed only on Android (android_native_app_glue.c is the NativeActivity
# entry-point shim) but FetchContent'd deps like libsndfile already enable it,
# so declaring it here is consistent and cheap on desktop.
project(yse-soundengine LANGUAGES C CXX)

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
option(YSE_BUILD_C_API
  "Build the extern \"C\" ABI bridge (yse_*) inside libyse so language bindings (Dart, Python, ...) can consume the DLL without C++ ABI compatibility"
  ON)
option(YSE_BUILD_TOOLS
  "Build developer tools (e.g. dump_patcher_meta — regenerates documentation/source/_data/patcher_objects.json). OFF in CI/release builds; turned on automatically by `python yse.py dump-patcher-meta`."
  OFF)
# YSE_ENABLE_PYTHON: embed a CPython interpreter for the live-coding DSL
# (epic #119, infra issue #124). Desktop only — Android is explicitly
# unsupported and fails fast below. OFF by default: when OFF, no Python
# headers, symbols, or runtime cost enter the build (byte-for-byte equivalent
# to a build without this option). Sourcing/isolation details live in
# cmake/YsePython.cmake; the interpreter lifecycle lives in YseEngine/python/.
option(YSE_ENABLE_PYTHON
  "Embed CPython interpreter for the live-coding DSL (desktop only)"
  OFF)
if(YSE_ENABLE_PYTHON AND ANDROID)
  message(FATAL_ERROR "YSE_ENABLE_PYTHON is not supported on Android.")
endif()
# YSE_ENABLE_MIDI_DEVICE: gate the RtMidi-backed MIDI device backend. Default
# ON on Windows/Linux desktop (preserves the prior unconditional behavior),
# OFF on Android (RtMidi isn't built there anyway) and Mac (already excluded
# in source). When OFF, RtMidi is not required at configure time and the
# midi/device.cpp + midi/midiDeviceManager.cpp sources are dropped — the rest
# of the engine (MIDI file playback, midiNote, etc.) is unaffected.
if(WIN32 OR (UNIX AND NOT APPLE AND NOT ANDROID))
  set(_yse_midi_device_default ON)
else()
  set(_yse_midi_device_default OFF)
endif()
option(YSE_ENABLE_MIDI_DEVICE
  "Build the RtMidi-backed MIDI device backend (Windows/Linux). When OFF, libyse builds without MIDI device I/O and does not require RtMidi at configure time."
  ${_yse_midi_device_default})
unset(_yse_midi_device_default)

# Export compile_commands.json for SonarCloud's C++ analyser and clangd.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Optional content pack (issue #179) ────────────────────────────────────────
# Defines YSE_CONTENT_PACK_DIR and the opt-in fetch/install path. With its
# options OFF (the default) this is a no-op — nothing is baked into the library.
include(${CMAKE_SOURCE_DIR}/cmake/YseContentPack.cmake)

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

# Embedded CPython (issue #124). Resolved here so the imported target and
# YSE_PYTHON_HOME are visible to the YseEngine/ and Tests/ subdirectories. The
# Android case has already been rejected with a FATAL_ERROR above.
if(YSE_ENABLE_PYTHON)
  include(${CMAKE_SOURCE_DIR}/cmake/YsePython.cmake)
endif()

if(ANDROID)
  # 16 KB page size compatibility — Pixel 8/9 and Android 15 QPR1+ devices may
  # run kernels with 16 KB pages, and PackageManager warns when installing APKs
  # whose native libraries have 4 KB-aligned LOAD segments. Aligning to 16 KB
  # makes the .so compatible with both page sizes at the cost of ~12 KB per
  # LOAD segment in the on-disk image. Required by Play Store from Nov 2025.
  add_link_options("-Wl,-z,max-page-size=16384")

  # The NDK toolchain provides no pkg-config in its sysroot. PortAudio and RtMidi
  # have no desktop counterpart here:
  #   • PortAudio is replaced by Oboe (audio I/O) — wired in a later phase.
  #   • RtMidi is unused — midi/device.cpp and midi/midiDeviceManager.cpp are
  #     `#if YSE_WINDOWS || YSE_LINUX`-guarded and compile to empty TUs.
  # libsndfile is built from source via FetchContent below — WAV-only, no
  # external codec libs (flac/ogg/vorbis/opus/mpeg).
  message(STATUS "Android build — fetching libsndfile from source")

  include(FetchContent)

  # libsndfile 1.2.2's top-level CMakeLists declares cmake_minimum_required(3.1),
  # which CMake 4.x rejects (compatibility with < 3.5 was removed). Forcing the
  # policy floor unblocks the fetched build without patching upstream.
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

  set(BUILD_PROGRAMS           OFF CACHE BOOL "" FORCE)
  set(BUILD_EXAMPLES           OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING            OFF CACHE BOOL "" FORCE)
  set(BUILD_REGTEST            OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS        OFF CACHE BOOL "" FORCE)
  set(ENABLE_EXTERNAL_LIBS     OFF CACHE BOOL "" FORCE)
  set(ENABLE_MPEG              OFF CACHE BOOL "" FORCE)
  set(ENABLE_CPACK             OFF CACHE BOOL "" FORCE)
  set(ENABLE_PACKAGE_CONFIG    OFF CACHE BOOL "" FORCE)
  set(INSTALL_PKGCONFIG_MODULE OFF CACHE BOOL "" FORCE)
  set(INSTALL_MANPAGES         OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    sndfile
    GIT_REPOSITORY https://github.com/libsndfile/libsndfile.git
    GIT_TAG        1.2.2
  )
  FetchContent_MakeAvailable(sndfile)

  # Oboe: Google's audio I/O wrapper. Replaces the deprecated direct OpenSL ES
  # backend. Oboe negotiates AAudio (API 26+, our minSdk) and falls back to
  # OpenSL ES on the rare device where AAudio is unavailable.
  FetchContent_Declare(
    oboe
    GIT_REPOSITORY https://github.com/google/oboe.git
    GIT_TAG        1.9.3
  )
  FetchContent_MakeAvailable(oboe)

  # Oboe's public headers trip -Wunused-parameter on virtuals like
  # reportWorkload(int32_t appWorkload). Mark its include dirs SYSTEM so the
  # diagnostic is suppressed in every TU that includes them transitively via
  # androidDeviceManager.h → oboeImplementation.h.
  get_target_property(_oboe_includes oboe INTERFACE_INCLUDE_DIRECTORIES)
  if(_oboe_includes)
    set_target_properties(oboe PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_oboe_includes}")
  endif()
else()
  find_package(PkgConfig REQUIRED)

  pkg_check_modules(PortAudio REQUIRED IMPORTED_TARGET portaudio-2.0)
  pkg_check_modules(SndFile   REQUIRED IMPORTED_TARGET sndfile)

  # RtMidi is the dependency of the MIDI device backend (midi/device.cpp,
  # midi/midiDeviceManager.cpp). Required when YSE_ENABLE_MIDI_DEVICE is ON,
  # which is the default on Windows/Linux. Set the option OFF to build libyse
  # on a desktop without RtMidi installed — at the cost of no MIDI device I/O.
  if(YSE_ENABLE_MIDI_DEVICE)
    pkg_check_modules(RtMidi REQUIRED IMPORTED_TARGET rtmidi)
    message(STATUS "RtMidi found — MIDI device backend enabled")
  else()
    message(STATUS "YSE_ENABLE_MIDI_DEVICE=OFF — skipping RtMidi dependency")
  endif()
endif()

# yse::sndfile — uniform interface target so YseEngine/CMakeLists.txt doesn't
# need a per-platform fork. On desktop it points at the pkg-config import
# (system libsndfile); on Android it points at the FetchContent-built static
# library above. A regular INTERFACE library (not IMPORTED) skips CMake's
# generation-time path validation, which the FetchContent target's
# build-time-only include dir would otherwise fail.
add_library(yse_sndfile_iface INTERFACE)
if(ANDROID)
  target_link_libraries(yse_sndfile_iface INTERFACE sndfile)
else()
  target_link_libraries(yse_sndfile_iface INTERFACE PkgConfig::SndFile)
endif()
add_library(yse::sndfile ALIAS yse_sndfile_iface)

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

if(YSE_BUILD_TOOLS)
  add_subdirectory(tools/dump_patcher_metadata)
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
