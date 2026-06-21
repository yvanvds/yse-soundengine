# YsePython.cmake — resolve the embedded CPython for YSE_ENABLE_PYTHON.
#
# Sourcing strategy (issue #124, epic #119): the issue text calls for
# "FetchContent fetches CPython at a pinned tag and builds it as a static
# library" with a build-time *frozen* stdlib subset. CPython ships no CMake
# build and does not support being built under the project's primary Windows
# toolchain (MSYS2 Clang64), so that path is impractical here. We instead use
# **Option C**: locate a system / prebuilt libpython via find_package and
# isolate it at *runtime* with PyConfig. The deviations this implies are:
#
#   * Linkage follows whatever the platform provides — typically the shared
#     libpython (on MSYS2 Clang64 only the import library
#     `libpython3.x.dll.a` exists), not a static archive. Deployments must
#     ship / locate the matching libpython at runtime.
#   * The interpreter version is whatever the host provides (>= 3.10 for the
#     PyConfig API), not a pinned 3.12.x.
#   * There is no build-time freeze. The runtime sees the full standard
#     library of the located interpreter, anchored via PyConfig.home and with
#     site-packages disabled (site_import = 0) so no third-party packages are
#     importable. "Curated frozen subset" therefore becomes "full stdlib,
#     isolated from site-packages".
#
# See docs/design/live_coding_dsl.md (§ value types / cross-references) and
# PROJECT_OVERVIEW.md "Python embedding" for the user-facing note. The
# embedding-time isolation lives in YseEngine/python/scriptRuntime.cpp.
#
# On success this module sets, in the including scope:
#   Python3::Python   — imported target with the embed include dirs + libpython
#   YSE_PYTHON_HOME   — native install prefix (forward-slashed) used as
#                       PyConfig.home so the interpreter resolves its stdlib.

# Anchor discovery to the active toolchain's own Python so we get an
# ABI-matched libpython. On MSYS2 Clang64 the compiler lives at
# <prefix>/bin/clang++.exe and ships <prefix>/bin/python3.exe with a mingw
# import library (libpython3.x.dll.a); without this hint FindPython3 would
# prefer a registry MSVC install (C:/PythonXX, python3xx.lib) whose COFF
# import lib the mingw linker cannot consume. Harmless on Linux (no registry).
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  get_filename_component(_yse_clang_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
  get_filename_component(_yse_clang_root "${_yse_clang_bin}" DIRECTORY)
  set(Python3_ROOT_DIR "${_yse_clang_root}")
  set(Python3_FIND_REGISTRY NEVER)
endif()

# Development.Embed pulls the libpython suitable for embedding (not the
# extension-module-only Development.Module component). 3.10 is the floor for
# the stable PyConfig / Py_InitializeFromConfig API we rely on.
find_package(Python3 3.10 REQUIRED COMPONENTS Development.Embed Interpreter)

# Anchor the embedded interpreter to the located install. sys.prefix is a
# native path (e.g. C:\msys64\clang64); TO_CMAKE_PATH forward-slashes it so it
# survives being baked into a C string literal on Windows.
execute_process(
  COMMAND "${Python3_EXECUTABLE}" -c "import sys; print(sys.prefix)"
  OUTPUT_VARIABLE _yse_python_prefix
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _yse_python_prefix_rc
)
if(NOT _yse_python_prefix_rc EQUAL 0)
  message(FATAL_ERROR "YSE_ENABLE_PYTHON: failed to query sys.prefix from ${Python3_EXECUTABLE}")
endif()
file(TO_CMAKE_PATH "${_yse_python_prefix}" YSE_PYTHON_HOME)

message(STATUS
  "YSE_ENABLE_PYTHON: embedding Python ${Python3_VERSION} "
  "(home: ${YSE_PYTHON_HOME}; lib: ${Python3_LIBRARIES})")
