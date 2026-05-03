# Tests/ — unit-test suite for YseEngine
# Gated behind YSE_BUILD_TESTS (default OFF); this file is only processed when
# that option is ON.  See the root CMakeLists.txt for the coverage option.

add_executable(yse_tests
  main.cpp
  test_sanity.cpp
  test_math_scalars.cpp
  test_math_converters.cpp
  test_buffer.cpp
)

# doctest is header-only; expose it as an INTERFACE library so the include path
# is propagated to any future test targets without repetition.
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE
  ${CMAKE_SOURCE_DIR}/dependencies
)

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
