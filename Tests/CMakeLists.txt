# Tests/ — unit-test suite for YseEngine
# Gated behind YSE_BUILD_TESTS (default OFF); this file is only processed when
# that option is ON.  See the root CMakeLists.txt for the coverage option.

set(YSE_TEST_SOURCES
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
  patcher/test_timerthread.cpp
  channel/test_channel.cpp
  channel/test_channel_concurrency.cpp
  sound/test_sound_state.cpp
  sound/test_sound_impl.cpp
  sound/test_sound_concurrency.cpp
  sound/test_sound_bufferIO.cpp
  sound/test_sound_manager.cpp
  reverb/test_reverb_dsp.cpp
  reverb/test_reverb_concurrency.cpp
  midi/test_midifile.cpp
  midi/test_devicemanager.cpp
  midi/test_midi_in.cpp
  music/test_scale.cpp
  music/test_note.cpp
  music/test_motif.cpp
  listener/test_listener.cpp
  io/test_bufferIO.cpp
  integration/test_device.cpp
)

if(ANDROID)
  # NativeActivity dynamically loads the test runner as libyse_tests.so. The
  # NDK ships android_native_app_glue (translates ANativeActivity callbacks
  # into a polled event queue consumable from android_main()); we compile its
  # single C source directly into the library.
  add_library(yse_tests SHARED
    ${YSE_TEST_SOURCES}
    support/android_asset_bridge.cpp
    "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c"
  )
  target_include_directories(yse_tests PRIVATE
    "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue"
  )
  target_link_libraries(yse_tests PRIVATE android)
  # android_native_app_glue.c uses ANativeActivity_onCreate as its entry
  # symbol; the linker would otherwise drop it as unused (no static refs).
  target_link_options(yse_tests PRIVATE "-u" "ANativeActivity_onCreate")
else()
  add_executable(yse_tests ${YSE_TEST_SOURCES})
endif()

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
# Suppress it here rather than editing the vendored header. The NDK's bundled
# Clang doesn't recognise -Wno-c2y-extensions (older diagnostic name), so skip
# on Android where -Wunknown-warning-option would otherwise fire on every TU.
if(NOT ANDROID)
  target_compile_options(yse_tests PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c2y-extensions>)
endif()

# Put the test binary in the same directory as the shared library so the OS
# dynamic-loader finds libyse.dll / libyse.so at runtime without extra rpath
# or PATH manipulation.
set_target_properties(yse_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Register with CTest. Skipped on Android — the test runner is a .so loaded by
# NativeActivity, not an executable, so CTest cannot drive it directly.
if(NOT ANDROID)
add_test(
  NAME    yse_unit_tests
  COMMAND yse_tests --test-suite-exclude=integration --duration=true
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_unit_tests PROPERTIES TIMEOUT 600)

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
  COMMAND yse_tests --test-suite=patcher --duration=true
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_patcher PROPERTIES LABELS patcher TIMEOUT 300)

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

add_test(
  NAME    yse_tests_io
  COMMAND yse_tests --test-suite=io
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_io PROPERTIES LABELS io)

# Concurrency stress tests for the SOUND/CHANNEL/REVERB managers.  These live
# in the sound/channel/reverb test suites but each test case name starts with
# "<sub> concurrency:" so doctest can filter by name.  Run via
# `ctest -L concurrency` or `yse_tests --test-case="* concurrency: *"`.
add_test(
  NAME    yse_tests_concurrency
  COMMAND yse_tests "--test-case=* concurrency: *"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_concurrency PROPERTIES LABELS concurrency)
endif()  # NOT ANDROID — close the CTest registration block

if(ANDROID)
  # The APK's android_main() extracts assets/fixtures/ into the NativeActivity's
  # internal data path; tests then load files from here via the existing macro.
  # The path is deterministic for a given applicationId — keep this in sync with
  # the namespace set in Tests/Android/app/build.gradle.kts.
  target_compile_definitions(yse_tests PRIVATE
    "YSE_TEST_FIXTURES_DIR=\"/data/user/0/net.attrx.yse.tests/files/fixtures\""
  )
else()
  target_compile_definitions(yse_tests PRIVATE
    "YSE_TEST_FIXTURES_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/support/fixtures\""
  )
endif()

if(NOT ANDROID)
  add_test(
    NAME    yse_tests_integration
    COMMAND yse_tests --test-suite=integration
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  set_tests_properties(yse_tests_integration PROPERTIES LABELS integration)
endif()

# Patcher test TUs include pObject.h and concrete node headers that use the
# same unused-parameter / json.hpp pragma suppressions applied to patcher
# source files in YseEngine/CMakeLists.txt.

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
  patcher/test_timerthread.cpp
  PROPERTIES COMPILE_FLAGS "${_patcher_test_suppress}"
)
