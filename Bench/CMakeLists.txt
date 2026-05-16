# Bench/ — performance benchmark suite for YseEngine
# Gated behind YSE_BUILD_BENCHMARKS (default OFF); this file is only processed
# when that option is ON.  Google Benchmark is fetched via FetchContent in the
# root CMakeLists.txt, so the `benchmark::benchmark` / `benchmark::benchmark_main`
# imported targets are available here.

add_executable(yse_benchmarks
  dsp/bench_buffer.cpp
)

# Match how Tests/ links: direct object linkage to yse_objects gives full
# symbol access without going through the API export boundary, which is the
# right tradeoff for a measurement tool — we want to call inline DSP
# functions the same way the engine does internally, not through the public
# DLL surface.
target_link_libraries(yse_benchmarks
  PRIVATE
    yse_objects
    benchmark::benchmark
    benchmark::benchmark_main
)

target_include_directories(yse_benchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(yse_benchmarks PRIVATE cxx_std_17)

# google-benchmark's BENCHMARK(...) macro expands to __COUNTER__, which newer
# Clang flags as a C2y extension.  Suppress on Clang the same way
# Tests/CMakeLists.txt does for doctest, rather than editing the vendored
# header.
target_compile_options(yse_benchmarks PRIVATE
  $<$<CXX_COMPILER_ID:Clang>:-Wno-c2y-extensions>
)

# Put the benchmark binary alongside the shared library so the OS dynamic
# loader finds libyse.dll / libyse.so at runtime without rpath or PATH
# tweaks.  Mirrors what Tests/CMakeLists.txt does for yse_tests.
set_target_properties(yse_benchmarks PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
