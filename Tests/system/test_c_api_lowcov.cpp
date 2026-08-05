// C-API boundary tests for the three engine-facing low-coverage translation
// units under YseEngine/c_api/ (issue #568, the follow-up half of #417 /
// epic #420): yse_system.cpp, yse_channel.cpp and yse_sound.cpp.
//
// As in test_c_api_surface.cpp the assertions are about the boundary contract,
// not DSP behaviour:
//
//   * NULL-handle handling on every entry point — void setters no-op, queries
//     return zero / false / NULL, and the YseStatus entry points distinguish
//     YSE_ERR_INVALID_HANDLE (bad handle) from YSE_ERR_INVALID_ARGUMENT (bad
//     argument).
//   * The snprintf string convention on yse_system_default_device /
//     _default_host / the MIDI device-name getters: the return is the full
//     length, cap == 0 or buf == NULL is a size query, and the buffer is
//     always NUL-terminated.
//   * Ownership — channel create/destroy, sound create/destroy, and the
//     borrowed pre-built channel singletons (never destroyed).
//   * Round-trips — every sound parameter through the flat ABI.
//
// Everything runs against the suite's shared offline engine
// (yse_system_init_offline), so no audio hardware is needed and it runs on
// headless CI.
//
// Deliberately NOT covered here, and why:
//
//   * yse_system_init() past its NULL guard. Opening the default hardware
//     device is exactly what CI does not have, and doing it on a developer
//     machine would seize the audio device mid-suite. The offline path
//     (init_offline) is the one under test.
//   * yse_system_open_device() past its two argument guards, for the same
//     reason — it hands the setup straight to the backend's device open.
//   * yse_system_get_device() past index 0 when the enumerated list is empty:
//     the engine's getDevice() indexes the device vector with an unchecked
//     operator[], so an out-of-range probe is undefined behaviour rather than
//     a catchable exception (issue #581). The cases below stay strictly inside
//     the enumerated range.
//   * yse_sound_restart() — leaves the implementation in SS_WANTSTORESTART,
//     which the file reader's intent ladder has no branch for, so the next
//     render block spins forever (issue #577).
//   * yse_sound_set_dsp(s, NULL) — the sound implementation dereferences the
//     message payload unconditionally and crashes on the render thread
//     (issue #578). The channel equivalent is safe and IS covered.
//   * Any setter or transport call on a YseSound that has not been loaded:
//     the engine interface dereferences a null pimpl before create()
//     (issue #579), so every case here drives a successfully loaded sound.
//
// yse_system_close() and yse_system_close_current_device() tear down
// process-global engine state, so they live in TEST_SUITE("capilowcovlife")
// at the bottom of this file and run in their own ctest process — the same
// isolation the lifecycle / synthlifecycle suites use.

#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "support/capilowcov_offline.hpp"

#include "yse_c/yse_channel.h"
#include "yse_c/yse_common.h"
#include "yse_c/yse_dsp.h"
#include "yse_c/yse_dsp_modules.h"
#include "yse_c/yse_enums.h"
#include "yse_c/yse_patcher.h"
#include "yse_c/yse_sound.h"
#include "yse_c/yse_system.h"

#ifndef YSE_TEST_FIXTURES_DIR
#define YSE_TEST_FIXTURES_DIR "../../Tests/support/fixtures"
#endif

namespace {

  const char* const kWavFixture = YSE_TEST_FIXTURES_DIR "/test_mono_44100.wav";

  // Read a snprintf-convention getter with the two-call size-then-fill pattern.
  template <typename Fn> std::string readString(Fn&& get) {
    const size_t need = get(nullptr, size_t{0});
    std::vector<char> buf(need + 1, '\0');
    get(buf.data(), buf.size());
    return std::string(buf.data());
  }

  // Load the bundled mono fixture into a fresh sound on the master channel and
  // pump until it reports ready. Returns nullptr when the engine or the fixture
  // is unavailable, in which case the caller skips.
  YseSound* makeLoadedSound(int loop = 0) {
    YseSound* s = yse_sound_create();
    if (!s) return nullptr;
    if (yse_sound_load_file(s, kWavFixture, yse_channel_master(), loop, 0.5f, 0) != YSE_OK) {
      yse_sound_destroy(s);
      return nullptr;
    }
    for (int i = 0; i < 100 && yse_sound_is_ready(s) == 0; ++i)
      capilowcov::pump(1);
    return s;
  }

} // namespace

TEST_SUITE("capilowcov") {

  // ═══ yse_system.cpp ═══════════════════════════════════════════════════════

  TEST_CASE("c-api system: every entry point is NULL-safe") {
    // The two init entry points reject a NULL handle before touching the
    // engine, which is the only branch of yse_system_init() that is reachable
    // without audio hardware.
    CHECK(yse_system_init(nullptr) == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_system_init_offline(nullptr) == YSE_ERR_INVALID_HANDLE);

    yse_system_render_offline(nullptr, 1);
    yse_system_update(nullptr);
    yse_system_close(nullptr);
    yse_system_pause(nullptr);
    yse_system_resume(nullptr);
    yse_system_sleep(nullptr, 1);
    yse_system_set_max_sounds(nullptr, 10);
    yse_system_audio_test(nullptr, 1);
    yse_system_auto_reconnect(nullptr, 1, 10);
    yse_system_close_current_device(nullptr);
    yse_system_set_underwater_depth(nullptr, 0.5f);
    yse_system_underwater_fx(nullptr, nullptr);

    CHECK(yse_system_missed_callbacks(nullptr) == 0);
    CHECK(yse_system_cpu_load(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_system_get_sample_rate(nullptr) == doctest::Approx(0.0));
    CHECK(yse_system_get_active_sample_rate(nullptr) == doctest::Approx(0.0));
    CHECK(yse_system_get_active_buffer_size(nullptr) == 0);
    CHECK(yse_system_get_active_output_latency(nullptr) == 0);
    CHECK(yse_system_get_max_sounds(nullptr) == 0);
    CHECK(yse_system_num_devices(nullptr) == 0u);
    CHECK(yse_system_get_device(nullptr, 0) == nullptr);
    CHECK(yse_system_get_global_reverb(nullptr) == nullptr);
    CHECK(yse_system_num_midi_in_devices(nullptr) == 0u);
    CHECK(yse_system_num_midi_out_devices(nullptr) == 0u);

    // Clock helpers reject both a NULL system and a NULL name.
    CHECK(yse_system_create_clock(nullptr, "c", 120.f) == 0);
    CHECK(yse_system_clock_exists(nullptr, "c") == 0);
    CHECK(yse_system_beat_position(nullptr, "c") == doctest::Approx(0.0));
    CHECK(yse_system_current_tempo(nullptr, "c") == doctest::Approx(0.0f));
    yse_system_destroy_clock(nullptr, "c");
    yse_system_set_tempo(nullptr, "c", 120.f, 0.f);

    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    CHECK(yse_system_create_clock(sys, nullptr, 120.f) == 0);
    CHECK(yse_system_clock_exists(sys, nullptr) == 0);
    CHECK(yse_system_beat_position(sys, nullptr) == doctest::Approx(0.0));
    CHECK(yse_system_current_tempo(sys, nullptr) == doctest::Approx(0.0f));
    yse_system_destroy_clock(sys, nullptr);
    yse_system_set_tempo(sys, nullptr, 120.f, 0.f);

    // Device / setup argument guards.
    CHECK(yse_system_open_device(nullptr, nullptr, YSE_CT_STEREO) == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_system_open_device(sys, nullptr, YSE_CT_STEREO) == YSE_ERR_INVALID_ARGUMENT);
    yse_system_underwater_fx(sys, nullptr);
  }

  TEST_CASE("c-api system: NULL handle clears every string out-parameter") {
    char buf[8];
    const auto dirty = [&buf] { std::memset(buf, 'x', sizeof(buf)); };

    dirty();
    CHECK(yse_system_default_device(nullptr, buf, sizeof(buf)) == 0u);
    CHECK(buf[0] == '\0');
    dirty();
    CHECK(yse_system_default_host(nullptr, buf, sizeof(buf)) == 0u);
    CHECK(buf[0] == '\0');
    dirty();
    CHECK(yse_system_midi_in_device_name(nullptr, 0, buf, sizeof(buf)) == 0u);
    CHECK(buf[0] == '\0');
    dirty();
    CHECK(yse_system_midi_out_device_name(nullptr, 0, buf, sizeof(buf)) == 0u);
    CHECK(buf[0] == '\0');

    // NULL buffers are accepted as size queries in the same NULL-handle path.
    CHECK(yse_system_default_device(nullptr, nullptr, 0) == 0u);
    CHECK(yse_system_default_host(nullptr, nullptr, 0) == 0u);
    CHECK(yse_system_midi_in_device_name(nullptr, 0, nullptr, 0) == 0u);
    CHECK(yse_system_midi_out_device_name(nullptr, 0, nullptr, 0) == 0u);
  }

  TEST_CASE("c-api system: offline session reports its runtime state") {
    if (!capilowcov::ensureOffline()) return; // engine unavailable → skip
    YseSystem* sys = yse_system_get();

    CHECK(yse_system_get_sample_rate(sys) > 0.0);
    CHECK(yse_system_get_active_sample_rate(sys) >= 0.0);
    CHECK(yse_system_get_active_buffer_size(sys) >= 0);
    CHECK(yse_system_get_active_output_latency(sys) >= 0);
    CHECK(yse_system_cpu_load(sys) >= 0.0f);
    CHECK(yse_system_missed_callbacks(sys) >= 0);
    CHECK(yse_system_get_global_reverb(sys) != nullptr);

    // maxSounds round-trips through the virtual-sound finder.
    const int before = yse_system_get_max_sounds(sys);
    yse_system_set_max_sounds(sys, 17);
    CHECK(yse_system_get_max_sounds(sys) == 17);
    yse_system_set_max_sounds(sys, before);
    CHECK(yse_system_get_max_sounds(sys) == before);

    // The remaining plain setters have no observable getter; assert only that
    // they are callable against a live offline session.
    yse_system_auto_reconnect(sys, 1, 5);
    yse_system_auto_reconnect(sys, 0, 0);
    yse_system_audio_test(sys, 1);
    yse_system_audio_test(sys, 0);
    yse_system_set_underwater_depth(sys, 0.5f);
    yse_system_set_underwater_depth(sys, 0.0f);
    yse_system_underwater_fx(sys, yse_channel_master());
    yse_system_sleep(sys, 1);

    // pause / resume are the reconnect primitives; both are no-ops with no
    // hardware stream attached, and the session must survive the pair.
    yse_system_pause(sys);
    yse_system_resume(sys);
    capilowcov::pump(2);
    CHECK(yse_system_get_sample_rate(sys) > 0.0);
  }

  TEST_CASE("c-api system: clock create / tempo / destroy round-trip") {
    if (!capilowcov::ensureOffline()) return;
    YseSystem* sys = yse_system_get();

    const char* name = "capilowcov.clock";
    CHECK(yse_system_clock_exists(sys, name) == 0);
    REQUIRE(yse_system_create_clock(sys, name, 120.0f) == 1);
    CHECK(yse_system_clock_exists(sys, name) == 1);
    CHECK(yse_system_current_tempo(sys, name) == doctest::Approx(120.0f));
    CHECK(yse_system_beat_position(sys, name) >= 0.0);

    yse_system_set_tempo(sys, name, 90.0f, 0.0f);
    capilowcov::pump(2);
    CHECK(yse_system_current_tempo(sys, name) == doctest::Approx(90.0f));

    yse_system_destroy_clock(sys, name);
    CHECK(yse_system_clock_exists(sys, name) == 0);
    // Queries against a destroyed clock answer with the documented zeroes.
    CHECK(yse_system_current_tempo(sys, name) == doctest::Approx(0.0f));
    CHECK(yse_system_beat_position(sys, name) == doctest::Approx(0.0));
  }

  TEST_CASE("c-api system: device / host name strings follow the snprintf convention") {
    if (!capilowcov::ensureOffline()) return;
    YseSystem* sys = yse_system_get();

    // Both are plain std::string copies out of the device manager. On headless
    // CI they are typically empty, which still has to obey the convention.
    for (int which = 0; which < 2; ++which) {
      const auto get = [sys, which](char* b, size_t c) {
        return which == 0 ? yse_system_default_device(sys, b, c)
                          : yse_system_default_host(sys, b, c);
      };
      const size_t need = get(nullptr, 0);
      char one = 'x';
      CHECK(get(&one, 0) == need); // cap == 0 must not write
      CHECK(one == 'x');

      std::vector<char> exact(need + 1, '\1');
      CHECK(get(exact.data(), exact.size()) == need);
      CHECK(std::strlen(exact.data()) == need);

      if (need > 1) {
        std::vector<char> small(need, '\1'); // one byte short
        CHECK(get(small.data(), small.size()) == need);
        CHECK(std::strlen(small.data()) == need - 1);
      }
    }
  }

  TEST_CASE("c-api system: device and MIDI enumeration stay inside their own range") {
    if (!capilowcov::ensureOffline()) return;
    YseSystem* sys = yse_system_get();

    // On headless CI the audio device list is empty, which is exactly the
    // branch under test: the count is the loop bound, and no descriptor is
    // requested when there is none.
    const unsigned int devices = yse_system_num_devices(sys);
    for (unsigned int i = 0; i < devices; ++i) {
      CHECK(yse_system_get_device(sys, i) != nullptr);
    }

    // The MIDI name getters clear the buffer first, then answer within range.
    // Out-of-range IDs are caught inside the C wrapper and reported as an
    // empty name rather than an exception across the ABI.
    char buf[64];
    const unsigned int midiIn = yse_system_num_midi_in_devices(sys);
    for (unsigned int i = 0; i < midiIn; ++i) {
      std::memset(buf, 'x', sizeof(buf));
      yse_system_midi_in_device_name(sys, i, buf, sizeof(buf));
      CHECK(std::strlen(buf) < sizeof(buf));
    }
    std::memset(buf, 'x', sizeof(buf));
    CHECK(yse_system_midi_in_device_name(sys, midiIn + 9999, buf, sizeof(buf)) == 0u);
    CHECK(buf[0] == '\0');

    const unsigned int midiOut = yse_system_num_midi_out_devices(sys);
    for (unsigned int i = 0; i < midiOut; ++i) {
      std::memset(buf, 'x', sizeof(buf));
      yse_system_midi_out_device_name(sys, i, buf, sizeof(buf));
      CHECK(std::strlen(buf) < sizeof(buf));
    }
    std::memset(buf, 'x', sizeof(buf));
    CHECK(yse_system_midi_out_device_name(sys, midiOut + 9999, buf, sizeof(buf)) == 0u);
    CHECK(buf[0] == '\0');
  }

  // ═══ yse_channel.cpp ══════════════════════════════════════════════════════

  TEST_CASE("c-api channel: every entry point is NULL-safe") {
    yse_channel_send(nullptr, 0, nullptr, 0.5f, 0);
    yse_channel_set_send_level(nullptr, 0, 0.5f);
    yse_channel_clear_send(nullptr, 0);
    yse_channel_set_dsp(nullptr, nullptr);
    yse_channel_set_volume(nullptr, 0.5f);
    yse_channel_move_to(nullptr, nullptr);
    yse_channel_attach_reverb(nullptr);
    yse_channel_set_virtual(nullptr, 1);
    yse_channel_destroy(nullptr);

    CHECK(yse_channel_get_send_level(nullptr, 0) == doctest::Approx(0.0f));
    CHECK(yse_channel_get_dsp(nullptr) == nullptr);
    CHECK(yse_channel_get_volume(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_channel_get_virtual(nullptr) == 0);
    CHECK(yse_channel_is_valid(nullptr) == 0);
    CHECK(yse_channel_is_return(nullptr) == 0);
    CHECK(std::string(yse_channel_get_name(nullptr)).empty());
    CHECK(yse_channel_get_num_outputs(nullptr) == 0);
    CHECK(yse_channel_get_peak_linear_pre(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_channel_get_peak_linear_post(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_channel_get_peak_db_pre(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_channel_get_peak_db_post(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_channel_get_peak_linear_pre_output(nullptr, 0) == doctest::Approx(0.0f));
    CHECK(yse_channel_get_peak_linear_post_output(nullptr, 0) == doctest::Approx(0.0f));
    CHECK(yse_channel_get_peak_db_pre_output(nullptr, 0) == doctest::Approx(0.0f));
    CHECK(yse_channel_get_peak_db_post_output(nullptr, 0) == doctest::Approx(0.0f));

    // The create entry points refuse a NULL name / parent and say why.
    yse_clear_last_error();
    CHECK(yse_channel_create(nullptr, nullptr) == nullptr);
    CHECK(std::string(yse_last_error()).find("non-null") != std::string::npos);
    CHECK(yse_channel_create_with_sends(nullptr, nullptr, 2) == nullptr);
    CHECK(yse_channel_create_return(nullptr, 2) == nullptr);
    yse_clear_last_error();
  }

  TEST_CASE("c-api channel: the six pre-built channels are distinct and live") {
    if (!capilowcov::ensureOffline()) return;

    YseChannel* const builtins[] = {yse_channel_master(),  yse_channel_fx(),    yse_channel_music(),
                                    yse_channel_ambient(), yse_channel_voice(), yse_channel_gui()};
    CHECK(yse_channel_get_num_outputs(builtins[0]) > 0); // master drives the device
    for (YseChannel* ch : builtins) {
      REQUIRE(ch != nullptr);
      CHECK(yse_channel_is_valid(ch) == 1);
      CHECK(yse_channel_is_return(ch) == 0);
      // Only the master owns device outputs; the five sub-channels mix into it
      // and report none of their own. Either way the count is never negative.
      CHECK(yse_channel_get_num_outputs(ch) >= 0);
      // Meters read zero with nothing rendered, but must be callable and
      // finite on both the combined and the per-output form.
      CHECK(yse_channel_get_peak_linear_pre(ch) >= 0.0f);
      CHECK(yse_channel_get_peak_linear_post(ch) >= 0.0f);
      CHECK(yse_channel_get_peak_db_pre(ch) <= 0.0f);
      CHECK(yse_channel_get_peak_db_post(ch) <= 0.0f);
      CHECK(yse_channel_get_peak_linear_pre_output(ch, 0) >= 0.0f);
      CHECK(yse_channel_get_peak_linear_post_output(ch, 0) >= 0.0f);
      CHECK(yse_channel_get_peak_db_pre_output(ch, 0) <= 0.0f);
      CHECK(yse_channel_get_peak_db_post_output(ch, 0) <= 0.0f);
    }

    // The five sub-channels are separate objects from the master and from
    // each other — a binding that mixed them up would still "work" until it
    // set a volume.
    for (size_t i = 1; i < sizeof(builtins) / sizeof(builtins[0]); ++i) {
      CHECK(builtins[i] != builtins[0]);
      CHECK(std::string(yse_channel_get_name(builtins[i])) !=
            std::string(yse_channel_get_name(builtins[0])));
    }
  }

  TEST_CASE("c-api channel: create / volume / virtual / reverb / move / destroy") {
    if (!capilowcov::ensureOffline()) return;

    YseChannel* ch = yse_channel_create("capilowcov.channel", yse_channel_master());
    REQUIRE(ch != nullptr);
    capilowcov::pump(10);

    CHECK(yse_channel_is_valid(ch) == 1);
    CHECK(yse_channel_is_return(ch) == 0);
    CHECK(std::string(yse_channel_get_name(ch)) == "capilowcov.channel");

    yse_channel_set_volume(ch, 0.25f);
    CHECK(yse_channel_get_volume(ch) == doctest::Approx(0.25f));
    yse_channel_set_volume(ch, 1.0f);
    CHECK(yse_channel_get_volume(ch) == doctest::Approx(1.0f));

    yse_channel_set_virtual(ch, 0);
    CHECK(yse_channel_get_virtual(ch) == 0);
    yse_channel_set_virtual(ch, 1);
    CHECK(yse_channel_get_virtual(ch) == 1);

    // An insert DSP object round-trips through the channel; the channel does
    // not take ownership, so it is destroyed separately after being detached.
    YseDspObject* eq = yse_dsp_eq_create();
    REQUIRE(eq != nullptr);
    yse_channel_set_dsp(ch, eq);
    CHECK(yse_channel_get_dsp(ch) == eq);
    capilowcov::pump(5);
    yse_channel_set_dsp(ch, nullptr);
    CHECK(yse_channel_get_dsp(ch) == nullptr);
    capilowcov::pump(5);
    yse_dsp_object_destroy(eq);

    yse_channel_attach_reverb(ch);
    capilowcov::pump(5);

    yse_channel_move_to(ch, yse_channel_fx());
    capilowcov::pump(5);
    CHECK(yse_channel_is_valid(ch) == 1);

    yse_channel_destroy(ch);
    capilowcov::pump(5);
  }

  TEST_CASE("c-api channel: return bus and send slots") {
    if (!capilowcov::ensureOffline()) return;

    YseChannel* ret = yse_channel_create_return("capilowcov.return", 1);
    REQUIRE(ret != nullptr);
    YseChannel* src = yse_channel_create_with_sends("capilowcov.source", yse_channel_master(), 2);
    REQUIRE(src != nullptr);
    capilowcov::pump(10);

    CHECK(yse_channel_is_return(ret) == 1);
    CHECK(yse_channel_is_return(src) == 0);

    CHECK(yse_channel_get_send_level(src, 0) == doctest::Approx(0.0f));
    yse_channel_send(src, 0, ret, 0.4f, 0);
    capilowcov::pump(5);
    CHECK(yse_channel_get_send_level(src, 0) == doctest::Approx(0.4f));

    yse_channel_set_send_level(src, 0, 0.8f);
    capilowcov::pump(5);
    CHECK(yse_channel_get_send_level(src, 0) == doctest::Approx(0.8f));

    yse_channel_clear_send(src, 0);
    capilowcov::pump(5);
    CHECK(yse_channel_get_send_level(src, 0) == doctest::Approx(0.0f));

    // A NULL return bus leaves the slot alone rather than clearing it.
    yse_channel_send(src, 1, nullptr, 0.5f, 1);
    CHECK(yse_channel_get_send_level(src, 1) == doctest::Approx(0.0f));

    yse_channel_destroy(src);
    yse_channel_destroy(ret);
    capilowcov::pump(10);
  }

  // ═══ yse_sound.cpp ════════════════════════════════════════════════════════

  TEST_CASE("c-api sound: create / destroy and the load failure paths") {
    YseSound* s = yse_sound_create();
    REQUIRE(s != nullptr);
    CHECK(yse_sound_is_valid(s) == 0); // nothing loaded yet

    // Handle guard vs argument guard are distinct status codes on all three
    // load entry points.
    CHECK(yse_sound_load_file(nullptr, "x.wav", nullptr, 0, 1.f, 0) == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_sound_load_file(s, nullptr, nullptr, 0, 1.f, 0) == YSE_ERR_INVALID_ARGUMENT);
    CHECK(yse_sound_load_buffer(nullptr, nullptr, nullptr, 0, 1.f) == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_sound_load_buffer(s, nullptr, nullptr, 0, 1.f) == YSE_ERR_INVALID_ARGUMENT);
    CHECK(yse_sound_load_patcher(nullptr, nullptr, nullptr, 1.f) == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_sound_load_patcher(s, nullptr, nullptr, 1.f) == YSE_ERR_INVALID_ARGUMENT);

    yse_sound_destroy(s);
    yse_sound_destroy(nullptr);
  }

  TEST_CASE("c-api sound: a missing file reports FILE_NOT_FOUND and names the path") {
    if (!capilowcov::ensureOffline()) return;

    YseSound* s = yse_sound_create();
    REQUIRE(s != nullptr);
    yse_clear_last_error();
    CHECK(yse_sound_load_file(s, "definitely_not_here.wav", nullptr, 0, 1.f, 0) ==
          YSE_ERR_FILE_NOT_FOUND);
    CHECK(std::string(yse_last_error()).find("definitely_not_here.wav") != std::string::npos);
    yse_clear_last_error();
    CHECK(yse_sound_is_valid(s) == 0);
    yse_sound_destroy(s);
    capilowcov::pump(5);
  }

  TEST_CASE("c-api sound: NULL-handle queries return zero without an engine") {
    CHECK(yse_sound_is_valid(nullptr) == 0);
    CHECK(yse_sound_is_ready(nullptr) == 0);
    CHECK(yse_sound_is_streaming(nullptr) == 0);
    CHECK(yse_sound_is_playing(nullptr) == 0);
    CHECK(yse_sound_is_paused(nullptr) == 0);
    CHECK(yse_sound_is_stopped(nullptr) == 0);
    CHECK(yse_sound_get_volume(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_sound_get_speed(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_sound_get_size(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_sound_get_spread(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_sound_get_looping(nullptr) == 0);
    CHECK(yse_sound_get_relative(nullptr) == 0);
    CHECK(yse_sound_get_doppler(nullptr) == 0);
    CHECK(yse_sound_get_pan2d(nullptr) == 0);
    CHECK(yse_sound_get_occlusion(nullptr) == 0);
    CHECK(yse_sound_get_time(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_sound_length(nullptr) == 0u);
    CHECK(yse_sound_get_dsp(nullptr) == nullptr);

    const yse_pos_t p = yse_sound_get_pos(nullptr);
    CHECK(p.x == doctest::Approx(0.0f));
    CHECK(p.y == doctest::Approx(0.0f));
    CHECK(p.z == doctest::Approx(0.0f));

    yse_sound_play(nullptr);
    yse_sound_pause(nullptr);
    yse_sound_stop(nullptr);
    yse_sound_toggle(nullptr);
    yse_sound_restart(nullptr);
    yse_sound_fade_and_stop(nullptr, 10);
    yse_sound_set_pos(nullptr, nullptr);
    yse_sound_set_volume(nullptr, 1.f, 0);
    yse_sound_set_speed(nullptr, 1.f);
    yse_sound_set_size(nullptr, 1.f);
    yse_sound_set_spread(nullptr, 1.f);
    yse_sound_set_looping(nullptr, 1);
    yse_sound_set_relative(nullptr, 1);
    yse_sound_set_doppler(nullptr, 1);
    yse_sound_set_pan2d(nullptr, 1);
    yse_sound_set_occlusion(nullptr, 1);
    yse_sound_set_time(nullptr, 1.f);
    yse_sound_set_dsp(nullptr, nullptr);
    yse_sound_move_to(nullptr, nullptr);
    CHECK(true); // reached here without dereferencing a NULL handle
  }

  TEST_CASE("c-api sound: file load and every parameter round-trip") {
    if (!capilowcov::ensureOffline()) return;

    YseSound* s = makeLoadedSound();
    if (!s) return; // fixture unavailable → skip
    CHECK(yse_sound_is_valid(s) == 1);
    CHECK(yse_sound_is_ready(s) == 1);
    CHECK(yse_sound_is_streaming(s) == 0); // loaded with streaming == 0
    CHECK(yse_sound_length(s) > 0u);

    // Position is stored on the interface side, so it reads back immediately.
    const yse_pos_t want{1.f, -2.f, 3.f};
    yse_sound_set_pos(s, &want);
    const yse_pos_t got = yse_sound_get_pos(s);
    CHECK(got.x == doctest::Approx(want.x));
    CHECK(got.y == doctest::Approx(want.y));
    CHECK(got.z == doctest::Approx(want.z));
    yse_sound_set_pos(s, nullptr); // NULL position is a no-op, not a reset
    CHECK(yse_sound_get_pos(s).x == doctest::Approx(want.x));

    yse_sound_set_volume(s, 0.75f, 0);
    CHECK(yse_sound_get_volume(s) == doctest::Approx(0.75f));
    yse_sound_set_volume(s, 0.25f, 50); // the faded form takes the same path
    CHECK(yse_sound_get_volume(s) == doctest::Approx(0.25f));

    yse_sound_set_speed(s, 1.5f);
    CHECK(yse_sound_get_speed(s) == doctest::Approx(1.5f));
    yse_sound_set_size(s, 4.0f);
    CHECK(yse_sound_get_size(s) == doctest::Approx(4.0f));
    yse_sound_set_spread(s, 0.6f);
    CHECK(yse_sound_get_spread(s) == doctest::Approx(0.6f));

    for (int on : {1, 0}) {
      yse_sound_set_looping(s, on);
      CHECK(yse_sound_get_looping(s) == on);
      yse_sound_set_relative(s, on);
      CHECK(yse_sound_get_relative(s) == on);
      yse_sound_set_doppler(s, on);
      CHECK(yse_sound_get_doppler(s) == on);
      yse_sound_set_pan2d(s, on);
      CHECK(yse_sound_get_pan2d(s) == on);
      yse_sound_set_occlusion(s, on);
      CHECK(yse_sound_get_occlusion(s) == on);
    }

    yse_sound_destroy(s);
    capilowcov::pump(10);
  }

  TEST_CASE("c-api sound: play / pause / toggle / stop drive the transport") {
    if (!capilowcov::ensureOffline()) return;

    // Looping, so the 100-frame fixture keeps playing long enough to observe
    // the transport states rather than reaching its end between two pumps.
    YseSound* s = makeLoadedSound(/*loop=*/1);
    if (!s) return;
    CHECK(yse_sound_is_stopped(s) == 1);

    // The intents are queued messages, so pump between them and read the state
    // back off the implementation.
    yse_sound_play(s);
    capilowcov::pump(10);
    CHECK(yse_sound_is_playing(s) == 1);

    yse_sound_pause(s);
    capilowcov::pump(10);
    CHECK(yse_sound_is_paused(s) == 1);

    yse_sound_toggle(s); // paused -> playing
    capilowcov::pump(10);
    CHECK(yse_sound_is_paused(s) == 0);

    yse_sound_stop(s);
    capilowcov::pump(10);
    CHECK(yse_sound_is_stopped(s) == 1);

    yse_sound_destroy(s);
    capilowcov::pump(10);
  }

  TEST_CASE("c-api sound: set_time seeks a playing sound") {
    if (!capilowcov::ensureOffline()) return;

    YseSound* s = makeLoadedSound(/*loop=*/1);
    if (!s) return;

    yse_sound_play(s);
    capilowcov::pump(10);
    yse_sound_set_time(s, 0.0f);
    capilowcov::pump(5);
    CHECK(yse_sound_get_time(s) >= 0.0f);

    yse_sound_destroy(s);
    capilowcov::pump(10);
  }

  TEST_CASE("c-api sound: fade_and_stop ramps down without stalling the render") {
    if (!capilowcov::ensureOffline()) return;

    YseSound* s = makeLoadedSound(/*loop=*/1);
    if (!s) return;

    yse_sound_play(s);
    capilowcov::pump(10);
    yse_sound_fade_and_stop(s, 20);
    capilowcov::pump(20);
    CHECK(yse_sound_is_valid(s) == 1);

    yse_sound_destroy(s);
    capilowcov::pump(10);
  }

  // yse_sound_restart() is deliberately NOT exercised: on a file- or
  // buffer-backed sound it leaves the implementation in SS_WANTSTORESTART,
  // which the file reader's intent ladder has no branch for, so the next
  // render block spins forever (issue #577). Add the case here once that is
  // fixed — restart() is the only sound entry point this suite leaves
  // uncovered.

  TEST_CASE("c-api sound: move_to re-parents a live sound") {
    if (!capilowcov::ensureOffline()) return;

    YseSound* s = makeLoadedSound();
    if (!s) return;

    yse_sound_move_to(s, yse_channel_fx());
    capilowcov::pump(5);
    yse_sound_move_to(s, nullptr); // NULL target is a no-op
    capilowcov::pump(5);
    CHECK(yse_sound_is_valid(s) == 1);

    yse_sound_destroy(s);
    capilowcov::pump(10);
  }

  TEST_CASE("c-api sound: buffer source and DSP insert") {
    if (!capilowcov::ensureOffline()) return;

    // A looping in-memory tone as the source — no file I/O at all.
    const unsigned int len = 1024;
    YseDspBuffer* buf = yse_dsp_buffer_create(len, 0);
    REQUIRE(buf != nullptr);
    std::vector<float> tone(len, 0.25f);
    REQUIRE(yse_dsp_buffer_write(buf, 0, tone.data(), len) == len);

    YseSound* s = yse_sound_create();
    REQUIRE(s != nullptr);
    REQUIRE(yse_sound_load_buffer(s, buf, yse_channel_master(), 1, 0.5f) == YSE_OK);
    for (int i = 0; i < 100 && yse_sound_is_ready(s) == 0; ++i)
      capilowcov::pump(1);
    CHECK(yse_sound_is_valid(s) == 1);
    // NB: the `loop` argument of yse_sound_load_buffer / _load_file reaches the
    // implementation but is not mirrored onto the interface, so the getter
    // still reads 0 here (issue #583). Setting it explicitly does round-trip.
    yse_sound_set_looping(s, 1);
    CHECK(yse_sound_get_looping(s) == 1);

    // The insert round-trips; the sound borrows it rather than owning it.
    // Detaching with yse_sound_set_dsp(s, NULL) is NOT exercised: unlike the
    // channel side, the sound implementation dereferences the message payload
    // unconditionally and crashes on the render thread (issue #578). The
    // insert is torn down by destroying the sound instead.
    YseDspObject* lp = yse_dsp_lowpass_create();
    REQUIRE(lp != nullptr);
    CHECK(yse_sound_get_dsp(s) == nullptr);
    yse_sound_set_dsp(s, lp);
    CHECK(yse_sound_get_dsp(s) == lp);
    capilowcov::pump(5);

    yse_sound_destroy(s);
    capilowcov::pump(10);
    yse_dsp_object_destroy(lp);
    yse_dsp_buffer_destroy(buf); // the buffer must outlive every sound using it
  }

  TEST_CASE("c-api sound: patcher source refuses a second owner") {
    if (!capilowcov::ensureOffline()) return;

    YsePatcher* patch = yse_patcher_create();
    REQUIRE(patch != nullptr);
    yse_patcher_init(patch, 2);

    YseSound* first = yse_sound_create();
    REQUIRE(first != nullptr);
    REQUIRE(yse_sound_load_patcher(first, patch, yse_channel_master(), 0.5f) == YSE_OK);
    capilowcov::pump(10);
    CHECK(yse_sound_is_valid(first) == 1);

    // One patcher per sound (#287): the second create is refused, reported as
    // an error status, and leaves the second sound invalid rather than
    // silently sharing the graph.
    YseSound* second = yse_sound_create();
    REQUIRE(second != nullptr);
    yse_clear_last_error();
    CHECK(yse_sound_load_patcher(second, patch, yse_channel_master(), 0.5f) == YSE_ERR_GENERIC);
    CHECK(std::string(yse_last_error()).find("already controlled") != std::string::npos);
    CHECK(yse_sound_is_valid(second) == 0);
    yse_clear_last_error();

    yse_sound_destroy(second);
    yse_sound_destroy(first);
    capilowcov::pump(10);
    yse_patcher_destroy(patch);
  }

} // TEST_SUITE("capilowcov")

// ═══ teardown half ══════════════════════════════════════════════════════════
//
// yse_system_close_current_device() and yse_system_close() tear down
// process-global engine state (the audio device, then the thread pools and the
// named bus). They cannot share a process with the cases above, so they run in
// their own ctest entry — the same treatment yse_tests_lifecycle gives
// System::close() at the C++ level.

TEST_SUITE("capilowcovlife") {

  TEST_CASE("c-api system: close_current_device then close tears the session down") {
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);

    yse_system_close(sys); // normalize regardless of starting state
    if (yse_system_init_offline(sys) != YSE_OK) return; // unavailable → skip
    CHECK(yse_system_get_sample_rate(sys) > 0.0);

    // Let any queued setup work drain before pulling the device.
    for (int i = 0; i < 10; ++i) {
      yse_system_update(sys);
      yse_system_render_offline(sys, 2);
    }

    yse_system_close_current_device(sys);
    // update() must stay safe with no device attached — this is the path a host
    // takes between closing one device and opening the next.
    yse_system_update(sys);

    yse_system_close(sys);
    yse_system_close(sys); // idempotent
    CHECK(true); // reached here without a crash on the teardown path
  }

} // TEST_SUITE("capilowcovlife")
