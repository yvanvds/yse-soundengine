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
  utils/test_interpolators.cpp
  dsp/test_filters.cpp
  dsp/test_delay.cpp
  dsp/test_envelope.cpp
  dsp/test_fourier.cpp
  dsp/test_modules.cpp
  dsp/test_module_filters.cpp
  dsp/test_module_delays.cpp
  patcher/test_graph.cpp
  patcher/test_nodes.cpp
  patcher/test_patcher_math.cpp
  patcher/test_math_wrappers.cpp
  patcher/test_dsp_objects.cpp
  patcher/test_generic_objects.cpp
  patcher/test_gui_objects.cpp
  channel/test_channel.cpp
  sound/test_sound_state.cpp
  reverb/test_reverb_dsp.cpp
  midi/test_midifile.cpp
  midi/test_devicemanager.cpp
  music/test_scale.cpp
  music/test_note.cpp
  music/test_motif.cpp
  listener/test_listener.cpp
  integration/test_device.cpp
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
    yse_objects   # direct object linkage: full symbol access without API export boundary
    doctest
)

target_compile_features(yse_tests PRIVATE cxx_std_17)

# doctest uses __COUNTER__, which Clang warns about as a C2y extension.
# Suppress it here rather than editing the vendored header.
target_compile_options(yse_tests PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c2y-extensions>)

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

add_test(
  NAME    yse_tests_patcher
  COMMAND yse_tests --test-suite=patcher
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_patcher PROPERTIES LABELS patcher)

add_test(
  NAME    yse_tests_channel
  COMMAND yse_tests --test-suite=channel
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_channel PROPERTIES LABELS channel)

add_test(
  NAME    yse_tests_sound
  COMMAND yse_tests --test-suite=sound
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_sound PROPERTIES LABELS sound)

add_test(
  NAME    yse_tests_reverb
  COMMAND yse_tests --test-suite=reverb
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_reverb PROPERTIES LABELS reverb)

add_test(
  NAME    yse_tests_midi
  COMMAND yse_tests --test-suite=midi
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_midi PROPERTIES LABELS midi)

add_test(
  NAME    yse_tests_music
  COMMAND yse_tests --test-suite=music
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_music PROPERTIES LABELS music)

add_test(
  NAME    yse_tests_listener
  COMMAND yse_tests --test-suite=listener
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_listener PROPERTIES LABELS listener)

target_compile_definitions(yse_tests PRIVATE
  "YSE_TEST_FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/support/fixtures\""
)

# Patcher test TUs include pObject.h and concrete node headers that use the
# same unused-parameter / json.hpp pragma suppressions applied to patcher
# source files in YseEngine/CMakeLists.txt.
add_test(
  NAME    yse_tests_integration
  COMMAND yse_tests --test-suite=integration
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_integration PROPERTIES
  LABELS   integration
  DISABLED TRUE
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(_patcher_test_suppress "-Wno-unused-parameter -Wno-deprecated-literal-operator -Wno-tautological-overlap-compare")
else()
  set(_patcher_test_suppress "-Wno-unused-parameter")
endif()
set_source_files_properties(
  patcher/test_graph.cpp
  patcher/test_nodes.cpp
  patcher/test_patcher_math.cpp
  patcher/test_math_wrappers.cpp
  patcher/test_dsp_objects.cpp
  patcher/test_generic_objects.cpp
  patcher/test_gui_objects.cpp
  PROPERTIES COMPILE_FLAGS "${_patcher_test_suppress}"
)
