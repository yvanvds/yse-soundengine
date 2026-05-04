# Tests/ — unit-test suite for YseEngine
# Gated behind YSE_BUILD_TESTS (default OFF); this file is only processed when
# that option is ON.  See the root CMakeLists.txt for the coverage option.

add_executable(yse_tests
  main.cpp
  test_sanity.cpp
  dsp/test_buffer.cpp
  dsp/test_ramp.cpp
  dsp/test_math_scalars.cpp
  dsp/test_math_converters.cpp
  dsp/test_oscillators.cpp
  utils/test_atomic.cpp
  utils/test_vector.cpp
  utils/test_lfqueue.cpp
  dsp/test_filters.cpp
  dsp/test_delay.cpp
  dsp/test_envelope.cpp
  dsp/test_fourier.cpp
  dsp/test_modules.cpp
)

# doctest is header-only; expose it as an INTERFACE library so the include path
# is propagated to any future test targets without repetition.
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE
  ${CMAKE_SOURCE_DIR}/dependencies
)

target_include_directories(yse_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(yse_tests
  PRIVATE
    yse
    doctest
)

target_compile_features(yse_tests PRIVATE cxx_std_17)

# doctest uses __COUNTER__, which Clang warns about as a C2y extension.
# Suppress it here rather than editing the vendored header.
target_compile_options(yse_tests PRIVATE -Wno-c2y-extensions)

# Put the test binary in the same directory as the shared library so the OS
# dynamic-loader finds libyse.dll / libyse.so at runtime without extra rpath
# or PATH manipulation.
set_target_properties(yse_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Register with CTest.
add_test(
  NAME    yse_unit_tests
  COMMAND yse_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(
  NAME    yse_tests_dsp
  COMMAND yse_tests --test-suite=dsp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_dsp PROPERTIES LABELS dsp)

add_test(
  NAME    yse_tests_utils
  COMMAND yse_tests --test-suite=utils
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_utils PROPERTIES LABELS utils)
