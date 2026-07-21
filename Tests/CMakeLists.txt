# Tests/ — unit-test suite for YseEngine
# Gated behind YSE_BUILD_TESTS (default OFF); this file is only processed when
# that option is ON.  See the root CMakeLists.txt for the coverage option.

set(YSE_TEST_SOURCES
  main.cpp
  test_sanity.cpp
  support/alloc_probe.cpp
  dsp/test_buffer.cpp
  dsp/test_ramp.cpp
  dsp/test_math_scalars.cpp
  dsp/test_math_converters.cpp
  dsp/test_oscillators.cpp
  utils/test_atomic.cpp
  utils/test_vector.cpp
  utils/test_lfqueue.cpp
  utils/test_mpmcqueue.cpp
  utils/test_interpolators.cpp
  utils/test_intrusive_list.cpp
  internal/test_threadpool.cpp
  dsp/test_filters.cpp
  dsp/test_delay.cpp
  dsp/test_envelope.cpp
  dsp/test_lfo_rate_robustness.cpp
  dsp/test_fourier.cpp
  dsp/test_modules.cpp
  dsp/test_module_filters.cpp
  dsp/test_module_delays.cpp
  dsp/test_module_feedback_delay.cpp
  dsp/test_module_chorus.cpp
  dsp/test_module_plate_reverb.cpp
  dsp/test_module_morphing_reverb.cpp
  dsp/test_module_underwater.cpp
  dsp/test_module_parametric_eq.cpp
  dsp/test_module_compressor.cpp
  dsp/test_module_multichannel.cpp
  dsp/test_synth_voice.cpp
  dsp/test_sfz_parser.cpp
  dsp/test_sampler_voice.cpp
  dsp/test_ladder_filter.cpp
  dsp/test_va_voice.cpp
  dsp/test_fm_voice.cpp
  dsp/test_dx7_sysex.cpp
  patcher/test_graph.cpp
  patcher/test_nodes.cpp
  patcher/test_patcher_math.cpp
  patcher/test_math_wrappers.cpp
  patcher/test_dsp_objects.cpp
  patcher/test_generic_objects.cpp
  patcher/test_gui_objects.cpp
  patcher/test_doc_coverage.cpp
  patcher/test_c_api_metadata.cpp
  patcher/test_patcher_bus.cpp
  patcher/test_patcher_graphstate.cpp
  patcher/test_patcher_setparams.cpp
  patcher/test_patcher_value_queue.cpp
  patcher/test_patcher_concurrency.cpp
  patcher/test_patcher_object_races.cpp
  patcher/test_patcher_feedback_loop.cpp
  patcher/test_patcher_adc.cpp
  patcher/test_timerthread.cpp
  channel/test_channel.cpp
  channel/test_channel_concurrency.cpp
  channel/test_channel_metering.cpp
  channel/test_channel_bus.cpp
  channel/test_channel_layout.cpp
  channel/test_channel_dsp.cpp
  channel/test_channel_sends.cpp
  channel/test_channel_underwater.cpp
  channel/test_c_api_channel.cpp
  sound/test_sound_state.cpp
  sound/test_sound_streaming.cpp
  sound/test_sound_bufferrace.cpp
  sound/test_sound_impl.cpp
  sound/test_sound_concurrency.cpp
  sound/test_sound_bufferIO.cpp
  sound/test_sound_manager.cpp
  sound/test_sound_file_gc.cpp
  sound/test_sound_bus.cpp
  sound/test_virtual_finder.cpp
  sound/test_manager_alloc.cpp
  synth/test_synth.cpp
  synth/test_synth_keyboard.cpp
  synth/test_synth_close.cpp
  synth/test_synth_positioning.cpp
  synth/test_synth_handlers.cpp
  synth/test_synth_bus.cpp
  synth/test_c_api_synth.cpp
  synth/test_c_api_instrument.cpp
  dsp/test_panner.cpp
  reverb/test_reverb_dsp.cpp
  reverb/test_reverb_concurrency.cpp
  midi/test_midifile.cpp
  midi/test_midifile_concurrency.cpp
  midi/test_midifile_alloc.cpp
  midi/test_devicemanager.cpp
  midi/test_midi_in.cpp
  midi/test_midi_out_sender.cpp
  midi/test_midi_synth_routing.cpp
  midi/test_midi_synth.cpp
  music/test_scale.cpp
  music/test_note.cpp
  music/test_motif.cpp
  music/test_player_alloc.cpp
  music/test_player_synth.cpp
  music/test_c_api_player.cpp
  listener/test_listener.cpp
  listener/test_listener_concurrency.cpp
  io/test_bufferIO.cpp
  io/test_callback_counter.cpp
  system/test_named_bus.cpp
  system/test_c_api_bus.cpp
  system/test_system_active_state.cpp
  system/test_lifecycle.cpp
  clock/test_domain_clocks.cpp
  clip/test_clip_transport.cpp
  integration/test_device.cpp
  integration/test_content_pack.cpp
  integration/test_synth_demos.cpp
  python/test_c_api_python.cpp
)

# Embedded-CPython runtime tests (issue #124) — only when the option is ON.
# The TU is guarded by YSE_ENABLE_PYTHON, which propagates from yse_objects
# (defined PUBLIC there) to this target via the yse_objects link below.
if(YSE_ENABLE_PYTHON)
  list(APPEND YSE_TEST_SOURCES python/test_script_runtime.cpp)
  list(APPEND YSE_TEST_SOURCES python/test_yse_module.cpp)
endif()

# Docs-coverage guard for the C-API reference (issue #398). Reads the source
# tree at test runtime (yse_c headers + documentation/source/api/c_api.rst)
# via the YSE_REPO_ROOT define below, which has no equivalent on an Android
# device — desktop only. Runs in the `system` suite and the monolithic entry.
if(NOT ANDROID)
  list(APPEND YSE_TEST_SOURCES system/test_api_doc_coverage.cpp)
  set_source_files_properties(system/test_api_doc_coverage.cpp PROPERTIES
    COMPILE_DEFINITIONS "YSE_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
  )
endif()

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
    yse::sndfile  # test_sound_streaming.cpp needs sndfile.hh (fixture writer + INTERNAL::soundFile)
)

# test_sound_streaming.cpp drives INTERNAL::soundFile directly, whose header is
# gated behind LIBSOUNDFILE_BACKEND (defined PRIVATE on yse_objects). Enable the
# gate for just this one TU so the rest of the suite is unaffected (issue #185).
set_source_files_properties(sound/test_sound_streaming.cpp PROPERTIES
  COMPILE_DEFINITIONS LIBSOUNDFILE_BACKEND
)

# test_sound_bufferrace.cpp also drives INTERNAL::soundFile directly (the
# DSP::buffer-backed read path, issue #286), so it needs the same gate enabled.
set_source_files_properties(sound/test_sound_bufferrace.cpp PROPERTIES
  COMPILE_DEFINITIONS LIBSOUNDFILE_BACKEND
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
  # `lifecycle`, `synthlifecycle`, `synthcapi`, `midisynth`, `playersynth` and
  # `playercapi` are excluded alongside `integration`: they drive System::close(),
  # which permanently stops the global thread pools and would break every later
  # suite sharing this process. Each runs in its own process via yse_tests_lifecycle
  # / yse_tests_synthlifecycle / yse_tests_synthcapi / yse_tests_midisynth /
  # yse_tests_playersynth / yse_tests_playercapi.
  #
  # The `* concurrency: *` stress cases run here again (issue #304). They were
  # temporarily excluded when the crowded ~1100-case process flaked (hang or
  # SIGSEGV) in the channel two-thread churn case; the root cause turned out to
  # be the channel impl ctor leaving `objectStatus` uninitialised, letting the
  # slow-pool deleteJob free a node mid-construction (issue #336, fixed in
  # PR #337). With that fixed, running the stress cases in this warm-heap,
  # thread-churning process is deliberate coverage — it is exactly the
  # environment that exposed #336 — on top of the dedicated
  # yse_tests_concurrency entry and the per-suite entries.
  # `sendstress` is excluded too: like the lifecycle suites it drives
  # System::initOffline() (process-global engine state), so it must run in its
  # own process (yse_tests_sendstress) rather than in this shared one.
  # `channelcapi` (issue #166 C-API channel-DSP/send surface) is excluded for
  # the same reason — it drives yse_system_init_offline() and runs in its own
  # process (yse_tests_channelcapi).
  # `clock` (issue #249 domain clocks) is excluded too: its cases drive
  # CLOCK::Manager().update() directly on the test thread to advance beats
  # deterministically, which would race a live audio thread from another suite
  # in this shared process. It runs isolated in yse_tests_clock.
  # `clip` (issue #250 clip transport) is excluded for the same reason as
  # `clock`: its cases drive CLOCK::Manager().update() / CLIP::Manager().update()
  # directly on the test thread. It runs isolated in yse_tests_clip.
  # `buscapi` (issue #389 host bus tap C API) is excluded like the other
  # c-api lifecycle suites: it drives yse_system_init_offline()/close() —
  # the tap-invalidation-at-close contract is part of the surface under
  # test — so it runs in its own process (yse_tests_buscapi).
  COMMAND yse_tests --test-suite-exclude=integration,lifecycle,synthlifecycle,synthpositioning,synthhandlers,synthbus,synthcapi,instrumentcapi,midisynth,playersynth,playercapi,sendstress,channelcapi,synthdemos,clock,clip,buscapi --duration=true
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_unit_tests PROPERTIES TIMEOUT 600)

# Engine init/close lifecycle (issue #132). Isolated in its own process because
# System::close() tears down process-global state (thread pools, named bus) that
# the rest of the suite assumes stays up. Uses initOffline(), so no audio
# hardware is needed and it runs in CI.
add_test(
  NAME    yse_tests_lifecycle
  COMMAND yse_tests --test-suite=lifecycle
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_lifecycle PROPERTIES LABELS lifecycle)

add_test(
  NAME    yse_tests_dsp
  COMMAND yse_tests --test-suite=dsp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_dsp PROPERTIES LABELS dsp)

# Domain clocks (issue #249). Isolated in its own process: the cases drive
# CLOCK::Manager().update() directly to advance beats deterministically, so
# they must not share a process with a suite that runs a live audio thread
# (which would also tick the clock manager). No audio device needed.
add_test(
  NAME    yse_tests_clock
  COMMAND yse_tests --test-suite=clock
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_clock PROPERTIES LABELS clock)

# Clip transport (issue #250). Isolated for the same reason as the clock suite:
# the cases drive CLOCK::Manager().update() / CLIP::Manager().update() directly
# on the test thread. No audio device needed.
add_test(
  NAME    yse_tests_clip
  COMMAND yse_tests --test-suite=clip
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_clip PROPERTIES LABELS clip)

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

# Patcher concurrency stress test (issue #229), registered on its own so the
# sanitizer CI legs can target just it (`ctest -R yse_tests_patcher_concurrency`)
# under TSan/ASan without paying for the whole suite — the `concurrency` label
# is shared with the channel/sound/reverb concurrency tests, so the sanitizer
# presets match by name, not by label. It also runs as part of yse_tests_patcher.
add_test(
  NAME    yse_tests_patcher_concurrency
  COMMAND yse_tests --test-case=concurrency:* --duration=true
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_patcher_concurrency PROPERTIES LABELS "patcher;concurrency" TIMEOUT 300)

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
  NAME    yse_tests_synth
  COMMAND yse_tests --test-suite=synth
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_synth PROPERTIES LABELS synth)

# Synth engine-close / offline lifecycle tests. Isolated in their own process
# for the same reason as yse_tests_lifecycle: they drive System::close(). Kept
# separate from the `lifecycle` process on purpose (see test_synth_close.cpp).
add_test(
  NAME    yse_tests_synthlifecycle
  COMMAND yse_tests --test-suite=synthlifecycle
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_synthlifecycle PROPERTIES LABELS "synth;lifecycle")

# Per-note positioned-sound infrastructure (issue #169). Isolated in its own
# process for the same reason as yse_tests_synthlifecycle: the note-storm, steal,
# and close-mid-swarm cases drive System::initOffline()/close(), which tear down
# process-global state the rest of the suite assumes stays up (#298). This is the
# process the ASan/TSan CI runs (tools/ci-linux/) to catch note-rate lifecycle
# races. initOffline() needs no audio hardware, so it runs in CI.
add_test(
  NAME    yse_tests_synthpositioning
  COMMAND yse_tests --test-suite=synthpositioning
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_synthpositioning PROPERTIES LABELS "synth;lifecycle")

# Position handlers (issue #170). Isolated in its own process for the same reason
# as yse_tests_synthpositioning: the lifecycle / steal cases drive
# System::initOffline()/close(). This process also runs under ASan/TSan in
# tools/ci-linux/ to catch handler-lifetime races. initOffline() needs no audio
# hardware, so it runs in CI.
add_test(
  NAME    yse_tests_synthhandlers
  COMMAND yse_tests --test-suite=synthhandlers
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_synthhandlers PROPERTIES LABELS "synth;lifecycle")

# Synth named-bus addressing (issue #388). Publishes synth.<name>.* note /
# controller events on the global bus and observes delivery through per-note
# position handlers. Isolated in its own process for the same reason as
# yse_tests_synthhandlers: every case drives System::initOffline()/close().
# initOffline() needs no audio hardware, so it runs in CI.
add_test(
  NAME    yse_tests_synthbus
  COMMAND yse_tests --test-suite=synthbus
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_synthbus PROPERTIES LABELS "synth;lifecycle")

# C-API synth surface (issue #157). Drives the synth entirely through the flat
# C ABI under an offline engine (yse_system_init_offline), then yse_system_close().
# Isolated in its own process for the same reason as yse_tests_synthlifecycle:
# System::close() tears down process-global state (thread pools) the rest of the
# suite assumes stays up (#298). initOffline() needs no audio hardware, so it
# runs in CI.
add_test(
  NAME    yse_tests_synthcapi
  COMMAND yse_tests --test-suite=synthcapi
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_synthcapi PROPERTIES LABELS "synth;c-api;lifecycle")

# C-API instrument surface (issue #178). Drives the sampler / VA / FM instrument
# voices and their loadable assets (SFZ instrument + DX7 bank) entirely through
# the flat C ABI under an offline engine (yse_system_init_offline), then
# yse_system_close(). Isolated in its own process for the same reason as
# yse_tests_synthcapi: System::close() tears down process-global state (thread
# pools) the rest of the suite assumes stays up (#298). initOffline() needs no
# audio hardware, so it runs in CI.
add_test(
  NAME    yse_tests_instrumentcapi
  COMMAND yse_tests --test-suite=instrumentcapi
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_instrumentcapi PROPERTIES LABELS "synth;c-api;lifecycle")

# C-API host bus tap surface (issue #389). Drives yse_bus_tap_create /
# destroy through the flat C ABI under an offline engine, including the
# yse_system_close() tap-invalidation contract. Isolated in its own process
# for the same reason as yse_tests_synthcapi: System::close() tears down
# process-global state (thread pools, named bus) the rest of the suite
# assumes stays up (#298). initOffline() needs no audio hardware, so it runs
# in CI.
add_test(
  NAME    yse_tests_buscapi
  COMMAND yse_tests --test-suite=buscapi
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_buscapi PROPERTIES LABELS "system;c-api;lifecycle")

add_test(
  NAME    yse_tests_midi
  COMMAND yse_tests --test-suite=midi
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_midi PROPERTIES LABELS midi)

# End-to-end MIDI -> synth routing (issue #155). Isolated in its own process for
# the same reason as yse_tests_synthlifecycle: it drives System::close() and
# constructs the synth + sound managers, whose static teardown must not leak
# into other suites (issue #298). Uses initOffline(), so it runs in CI.
add_test(
  NAME    yse_tests_midisynth
  COMMAND yse_tests --test-suite=midisynth
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_midisynth PROPERTIES LABELS "midi;synth;lifecycle")

# Synth & effects demo scenes (issue #180). Isolated in its own process for the
# same reason as yse_tests_midisynth: it drives System::initOffline()/close() and
# builds the synth + channel managers, whose static teardown must not leak into
# other suites (issue #298). Loads the bundled content pack (skips gracefully if
# absent) and uses initOffline(), so it runs in CI.
add_test(
  NAME    yse_tests_synthdemos
  COMMAND yse_tests --test-suite=synthdemos
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_synthdemos PROPERTIES LABELS "synth;infra;lifecycle")

add_test(
  NAME    yse_tests_music
  COMMAND yse_tests --test-suite=music
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_music PROPERTIES LABELS music)

# End-to-end generative-player -> synth routing (issue #156). Isolated in its own
# process for the same reason as yse_tests_midisynth: it drives System::close()
# and constructs the synth + sound managers, whose static teardown must not leak
# into other suites (issue #298). Uses initOffline(), so it runs in CI.
add_test(
  NAME    yse_tests_playersynth
  COMMAND yse_tests --test-suite=playersynth
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_playersynth PROPERTIES LABELS "music;synth;lifecycle")

# C-API generative-player surface (issue #268). Drives the player create -> play
# path entirely through the flat C ABI under an offline engine, then
# yse_system_close(). Isolated in its own process for the same reason as
# yse_tests_synthcapi / yse_tests_playersynth: System::close() tears down
# process-global state (thread pools) the rest of the suite assumes stays up
# (#298). initOffline() needs no audio hardware, so it runs in CI.
add_test(
  NAME    yse_tests_playercapi
  COMMAND yse_tests --test-suite=playercapi
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_playercapi PROPERTIES LABELS "music;synth;c-api;lifecycle")

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

add_test(
  NAME    yse_tests_system
  COMMAND yse_tests --test-suite=system
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_system PROPERTIES LABELS system)

# Embedded-CPython runtime suite (issue #124). Only present when the option is
# ON; otherwise the suite has no test cases and registering it would fail.
if(YSE_ENABLE_PYTHON)
  add_test(
    NAME    yse_tests_python
    COMMAND yse_tests --test-suite=python
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  set_tests_properties(yse_tests_python PROPERTIES LABELS python)
endif()

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

# Send/return offline-render + threading gate (issue #165). Runs under an
# offline engine (no audio device), so it executes on headless CI and under the
# TSan/ASan sanitizer legs — the `tests-tsan` / `tests-asan` presets include
# this entry by name (see CMakePresets.json). Isolated in its own process
# because it drives System::initOffline(); shares the `concurrency` label with
# the other stress suites.
add_test(
  NAME    yse_tests_sendstress
  COMMAND yse_tests --test-suite=sendstress --duration=true
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_sendstress PROPERTIES LABELS "channel;concurrency" TIMEOUT 300)

# C-API channel-DSP, effect-module, and send/return surface (issue #166). Drives
# the channel insert + send-to-plate-return graph entirely through the flat C
# ABI under an offline engine (yse_system_init_offline), so it runs on headless
# CI. Isolated in its own process for the same reason as yse_tests_sendstress:
# it drives process-global engine state via init_offline().
add_test(
  NAME    yse_tests_channelcapi
  COMMAND yse_tests --test-suite=channelcapi
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
set_tests_properties(yse_tests_channelcapi PROPERTIES LABELS "channel;c-api" TIMEOUT 300)
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

# Content-pack smoke test (issue #179): point the test at the pack root. The
# test skips gracefully when the directory or an individual asset is absent, so
# on Android (where this host path does not exist on-device) it is a no-op.
target_compile_definitions(yse_tests PRIVATE
  "YSE_CONTENT_PACK_DIR=\"${YSE_CONTENT_PACK_DIR}\""
)

if(NOT ANDROID)
  add_test(
    NAME    yse_tests_integration
    COMMAND yse_tests --test-suite=integration
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  set_tests_properties(yse_tests_integration PROPERTIES LABELS integration)

  # Content-pack smoke test (issue #179). Offline file loads only (no device,
  # no engine init), so it runs on headless CI in its own suite.
  add_test(
    NAME    yse_tests_contentpack
    COMMAND yse_tests --test-suite=contentpack
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
  set_tests_properties(yse_tests_contentpack PROPERTIES LABELS "contentpack;infra")
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
  patcher/test_doc_coverage.cpp
  patcher/test_c_api_metadata.cpp
  patcher/test_patcher_bus.cpp
  patcher/test_patcher_graphstate.cpp
  patcher/test_patcher_setparams.cpp
  patcher/test_patcher_value_queue.cpp
  patcher/test_patcher_concurrency.cpp
  patcher/test_patcher_object_races.cpp
  patcher/test_patcher_feedback_loop.cpp
  patcher/test_patcher_adc.cpp
  patcher/test_timerthread.cpp
  PROPERTIES COMPILE_FLAGS "${_patcher_test_suppress}"
)
