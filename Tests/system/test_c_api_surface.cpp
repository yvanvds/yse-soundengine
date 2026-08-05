// C-API boundary tests for the zero-coverage translation units under
// YseEngine/c_api/ (issue #417, part of the SonarCloud quality-gate epic #420):
// yse_device.cpp, yse_reverb.cpp, yse_log.cpp, yse_listener.cpp and
// yse_buffer_io.cpp. (yse_clip.cpp is covered from Tests/clip/test_c_api_clip.cpp,
// which reuses the clock/clip-manager isolation the `clip` suite already has.)
//
// The assertions are about the *boundary contract* rather than DSP behaviour,
// because that is what a binding breaks on silently:
//
//   * NULL-handle handling — every entry point is exercised with a NULL handle
//     and, where it takes one, a NULL out-parameter. Void setters must no-op;
//     queries must return zero / false / NULL and clear the out buffer.
//   * Ownership and lifetime — create/destroy pairs, destroy(NULL).
//   * String marshalling — the snprintf convention documented in yse_device.h
//     (size query with cap == 0 or buf == NULL, exact fit, truncation, always
//     NUL-terminated). This is also the `strlen` hotspot path in yse_log.cpp.
//   * Enum round-trips — YseErrorLevel and YseReverbPreset in both directions.
//
// The engine runs offline (yse_system_init_offline), so no audio hardware is
// needed and the suite runs on headless CI. It lives in its own
// TEST_SUITE("capisurface") and its own ctest process (see Tests/CMakeLists.txt)
// for the same reason as the buscapi / channelcapi suites: init_offline() drives
// process-global engine state, and on top of that the log cases swap the global
// log sink while the BufferIO cases install the global VFS hooks.
//
// A few cases reach past the C ABI into the engine headers — building a device
// descriptor (the engine's enumerator is the only other producer, and it needs
// real hardware) and reading back deviceSetup::getOutputChannels(), which has no
// C getter. That mirrors what test_c_api_bus.cpp does for bus publishes: the
// tests link yse_objects with full symbol access.

#include <doctest/doctest.h>

#include <cstring>
#include <mutex>
#include <new> // placement new — constructs a device over pre-dirtied storage
#include <stdexcept>
#include <string>
#include <vector>

#include "yse_c/yse_buffer_io.h"
#include "yse_c/yse_common.h"
#include "yse_c/yse_device.h"
#include "yse_c/yse_enums.h"
#include "yse_c/yse_listener.h"
#include "yse_c/yse_log.h"
#include "yse_c/yse_reverb.h"
#include "yse_c/yse_system.h"

#include "device/deviceInterface.hpp"
#include "device/deviceSetup.hpp"

namespace {

  // Init the offline engine once for the whole capisurface process. Mirrors the
  // channelcapi suite's ensureOffline(); no audio hardware needed.
  bool ensureOffline() {
    static bool done = false;
    static bool ok = false;
    if (!done) {
      ok = (yse_system_init_offline(yse_system_get()) == YSE_OK);
      done = true;
    }
    return ok;
  }

  // A fully populated device descriptor. The engine's enumerator is the only
  // other producer of these and it needs real hardware, so the tests build one
  // by hand and hand the C API the same reinterpret_cast the engine does in
  // yse_system_get_device(). Every scalar field is set explicitly so the
  // getters have distinguishable values to mirror (the constructor itself
  // zero-initialises them — see the issue #565 case below).
  YSE::device makeDevice() {
    YSE::device d;
    d.setName("Test Output Device") // 18 chars — used by the truncation cases
        .setTypeName("TestHost")
        .addOutputChannelName("out 1")
        .addOutputChannelName("out 2")
        .addInputChannelName("in 1")
        .addAvailableSampleRate(44100.0)
        .addAvailableSampleRate(48000.0)
        .addAvailableBufferSize(256)
        .addAvailableBufferSize(512)
        .setDefaultBufferSize(512)
        .setOutputLatency(128)
        .setInputLatency(64)
        .setID(7);
    return d;
  }

  YseDevice* handle(YSE::device& d) {
    return reinterpret_cast<YseDevice*>(&d);
  }

  // Collects the strings the C log callback hands over, taking ownership of each
  // one exactly as the header tells a host to (yse_log_free_message). The mutex
  // is not decoration: while the bridge is installed, any engine thread that
  // emits a log line lands here — which is also why the assertions below count
  // occurrences of a unique token rather than total messages.
  struct LogSink {
    std::mutex m;
    std::vector<std::string> messages;

    int countMatching(const std::string& needle) {
      std::scoped_lock lk(m);
      int n = 0;
      for (const std::string& msg : messages)
        if (msg.find(needle) != std::string::npos) ++n;
      return n;
    }
  };

  void YSE_C_CALLBACK logReceive(char* msg, void* user_data) {
    auto* sink = static_cast<LogSink*>(user_data);
    {
      std::scoped_lock lk(sink->m);
      sink->messages.emplace_back(msg != nullptr ? msg : "");
    }
    // The bridge malloc'd this copy and handed ownership over — release it the
    // way the header documents.
    yse_log_free_message(msg);
  }

  // Restores the process-global log state (sink, level, log file) whichever way
  // a case exits, so ordering between the log cases is irrelevant.
  struct LogStateGuard {
    YseLog* log = yse_log_get();
    YseErrorLevel level = yse_log_get_level(yse_log_get());
    ~LogStateGuard() {
      yse_log_set_callback(log, nullptr, nullptr);
      yse_log_set_level(log, level);
      yse_log_set_logfile(log, "YSElog.txt");
    }
  };

} // namespace

TEST_SUITE("capisurface") {

  // ─── yse_device.cpp: descriptor ────────────────────────────────────────────

  TEST_CASE("c-api device: descriptor getters mirror the engine device") {
    YSE::device d = makeDevice();
    YseDevice* dev = handle(d);
    char buf[64] = {0};

    CHECK(yse_device_get_name(dev, buf, sizeof(buf)) == 18);
    CHECK(std::string(buf) == "Test Output Device");
    CHECK(yse_device_get_type_name(dev, buf, sizeof(buf)) == 8);
    CHECK(std::string(buf) == "TestHost");

    CHECK(yse_device_num_output_channels(dev) == 2);
    CHECK(yse_device_get_output_channel_name(dev, 1, buf, sizeof(buf)) == 5);
    CHECK(std::string(buf) == "out 2");

    CHECK(yse_device_num_input_channels(dev) == 1);
    CHECK(yse_device_get_input_channel_name(dev, 0, buf, sizeof(buf)) == 4);
    CHECK(std::string(buf) == "in 1");

    CHECK(yse_device_num_sample_rates(dev) == 2);
    CHECK(yse_device_get_sample_rate(dev, 0) == doctest::Approx(44100.0));
    CHECK(yse_device_get_sample_rate(dev, 1) == doctest::Approx(48000.0));

    CHECK(yse_device_num_buffer_sizes(dev) == 2);
    CHECK(yse_device_get_buffer_size(dev, 0) == 256);
    CHECK(yse_device_get_buffer_size(dev, 1) == 512);

    CHECK(yse_device_default_buffer_size(dev) == 512);
    CHECK(yse_device_output_latency(dev) == 128);
    CHECK(yse_device_input_latency(dev) == 64);
    CHECK(yse_device_get_id(dev) == 7);
  }

  TEST_CASE("c-api device: string-out follows the snprintf convention") {
    YSE::device d = makeDevice();
    YseDevice* dev = handle(d);

    // Size query: no buffer, or a zero capacity, still reports the full length.
    CHECK(yse_device_get_name(dev, nullptr, 0) == 18);
    CHECK(yse_device_get_name(dev, nullptr, 64) == 18);
    char probe = '@';
    CHECK(yse_device_get_name(dev, &probe, 0) == 18);
    CHECK(probe == '@'); // cap == 0 must not write

    // Exact fit: length + terminator.
    char exact[19] = {0};
    CHECK(yse_device_get_name(dev, exact, sizeof(exact)) == 18);
    CHECK(std::string(exact) == "Test Output Device");

    // Truncation: cap - 1 bytes plus a terminator, return value unchanged.
    char small[5] = {0};
    CHECK(yse_device_get_name(dev, small, sizeof(small)) == 18);
    CHECK(std::string(small) == "Test");
    CHECK(small[4] == '\0');

    // cap == 1 leaves an empty, still-terminated string.
    char one[1] = {'x'};
    CHECK(yse_device_get_name(dev, one, sizeof(one)) == 18);
    CHECK(one[0] == '\0');

    // The same convention holds for the indexed channel-name getters.
    char chan[3] = {0};
    CHECK(yse_device_get_output_channel_name(dev, 0, chan, sizeof(chan)) == 5);
    CHECK(std::string(chan) == "ou");
    CHECK(yse_device_get_input_channel_name(dev, 0, chan, sizeof(chan)) == 4);
    CHECK(std::string(chan) == "in");
  }

  TEST_CASE("c-api device: a NULL descriptor reads as empty") {
    char buf[8];
    std::memset(buf, 'x', sizeof(buf));

    CHECK(yse_device_get_name(nullptr, buf, sizeof(buf)) == 0);
    CHECK(buf[0] == '\0');
    std::memset(buf, 'x', sizeof(buf));
    CHECK(yse_device_get_type_name(nullptr, buf, sizeof(buf)) == 0);
    CHECK(buf[0] == '\0');
    std::memset(buf, 'x', sizeof(buf));
    CHECK(yse_device_get_output_channel_name(nullptr, 0, buf, sizeof(buf)) == 0);
    CHECK(buf[0] == '\0');
    std::memset(buf, 'x', sizeof(buf));
    CHECK(yse_device_get_input_channel_name(nullptr, 0, buf, sizeof(buf)) == 0);
    CHECK(buf[0] == '\0');

    // No out buffer at all is still safe.
    CHECK(yse_device_get_name(nullptr, nullptr, 0) == 0);
    CHECK(yse_device_get_type_name(nullptr, nullptr, 0) == 0);
    CHECK(yse_device_get_output_channel_name(nullptr, 0, nullptr, 0) == 0);
    CHECK(yse_device_get_input_channel_name(nullptr, 0, nullptr, 0) == 0);

    CHECK(yse_device_num_output_channels(nullptr) == 0);
    CHECK(yse_device_num_input_channels(nullptr) == 0);
    CHECK(yse_device_num_sample_rates(nullptr) == 0);
    CHECK(yse_device_get_sample_rate(nullptr, 0) == doctest::Approx(0.0));
    CHECK(yse_device_num_buffer_sizes(nullptr) == 0);
    CHECK(yse_device_get_buffer_size(nullptr, 0) == 0);
    CHECK(yse_device_default_buffer_size(nullptr) == 0);
    CHECK(yse_device_output_latency(nullptr) == 0);
    CHECK(yse_device_input_latency(nullptr) == 0);
    CHECK(yse_device_get_id(nullptr) == 0);
  }

  TEST_CASE("c-api device: out-of-range indices read as empty (issue #565)") {
    // Before the fix the engine getters indexed with operator[], so the
    // try/catch in the C API could never fire and the two scalar getters had
    // no guard at all: an FFI consumer iterating with a stale count (a device
    // list refreshed between yse_device_num_output_channels() and the name
    // loop) got a silent out-of-bounds read. The engine now bound-checks with
    // .at() and the C API translates that into the NULL-handle contract.
    YSE::device d = makeDevice();
    YseDevice* dev = handle(d);

    char buf[16];
    std::memset(buf, 'x', sizeof(buf));
    CHECK(yse_device_get_output_channel_name(dev, 5, buf, sizeof(buf)) == 0);
    CHECK(buf[0] == '\0');
    std::memset(buf, 'x', sizeof(buf));
    CHECK(yse_device_get_input_channel_name(dev, 5, buf, sizeof(buf)) == 0);
    CHECK(buf[0] == '\0');

    // The first index past the end is the one a stale count actually hits.
    std::memset(buf, 'x', sizeof(buf));
    CHECK(yse_device_get_output_channel_name(dev, yse_device_num_output_channels(dev), buf,
                                             sizeof(buf)) == 0);
    CHECK(buf[0] == '\0');
    std::memset(buf, 'x', sizeof(buf));
    CHECK(yse_device_get_input_channel_name(dev, yse_device_num_input_channels(dev), buf,
                                            sizeof(buf)) == 0);
    CHECK(buf[0] == '\0');

    // No out buffer at all on an out-of-range index is still safe.
    CHECK(yse_device_get_output_channel_name(dev, 5, nullptr, 0) == 0);
    CHECK(yse_device_get_input_channel_name(dev, 5, nullptr, 0) == 0);

    CHECK(yse_device_get_sample_rate(dev, 5) == doctest::Approx(0.0));
    CHECK(yse_device_get_sample_rate(dev, yse_device_num_sample_rates(dev)) ==
          doctest::Approx(0.0));
    CHECK(yse_device_get_buffer_size(dev, 5) == 0);
    CHECK(yse_device_get_buffer_size(dev, yse_device_num_buffer_sizes(dev)) == 0);

    // An empty descriptor has no valid index at all.
    YSE::device empty;
    YseDevice* none = handle(empty);
    std::memset(buf, 'x', sizeof(buf));
    CHECK(yse_device_get_output_channel_name(none, 0, buf, sizeof(buf)) == 0);
    CHECK(buf[0] == '\0');
    std::memset(buf, 'x', sizeof(buf));
    CHECK(yse_device_get_input_channel_name(none, 0, buf, sizeof(buf)) == 0);
    CHECK(buf[0] == '\0');
    CHECK(yse_device_get_sample_rate(none, 0) == doctest::Approx(0.0));
    CHECK(yse_device_get_buffer_size(none, 0) == 0);
  }

  TEST_CASE("device: indexed getters throw rather than read past the end (issue #565)") {
    // The C++ side of the same fix: the engine getters bound-check, so a C++
    // consumer gets a defined std::out_of_range instead of undefined
    // behaviour. This is what makes the C API's catch reachable.
    YSE::device d = makeDevice();
    const YSE::device& c = d;

    CHECK_THROWS_AS((void)c.getOutputChannelName(5), std::out_of_range);
    CHECK_THROWS_AS((void)c.getInputChannelName(5), std::out_of_range);
    CHECK_THROWS_AS((void)c.getAvailableSampleRate(5), std::out_of_range);
    CHECK_THROWS_AS((void)c.getAvailableBufferSize(5), std::out_of_range);

    // In-range indices are untouched.
    CHECK(c.getOutputChannelName(1) == "out 2");
    CHECK(c.getAvailableBufferSize(1) == 512);
  }

  TEST_CASE("device: the constructor zero-initialises the scalar fields (issue #565)") {
    // defaultBufferSize, inputLatency, outputLatency and ID were left out of
    // the constructor, so yse_device_default_buffer_size() and friends
    // returned whatever was in that memory unless the enumerator happened to
    // set every one of them.
    //
    // A plain stack-local device is an unreliable regression test — the slot
    // is often already zero. Placement-new over 0xFF-filled storage (which
    // reads back as -1 for an int) makes the uninitialised read deterministic,
    // the same trick test_reverb_dsp.cpp uses for issue #263.
    alignas(YSE::device) unsigned char storage[sizeof(YSE::device)];
    std::memset(storage, 0xFF, sizeof(storage));
    YSE::device* d = new (storage) YSE::device();
    YseDevice* dev = handle(*d);

    CHECK(yse_device_default_buffer_size(dev) == 0);
    CHECK(yse_device_output_latency(dev) == 0);
    CHECK(yse_device_input_latency(dev) == 0);
    CHECK(yse_device_get_id(dev) == 0);

    d->~device();
  }

  // ─── yse_device.cpp: deviceSetup ───────────────────────────────────────────

  TEST_CASE("c-api device setup: create/destroy and the setters write through") {
    YseDeviceSetup* setup = yse_device_setup_create();
    REQUIRE(setup != nullptr);

    YSE::device in = makeDevice();
    YSE::device out = makeDevice();
    yse_device_setup_set_input(setup, handle(in));
    yse_device_setup_set_output(setup, handle(out));
    yse_device_setup_set_sample_rate(setup, 48000.0);
    yse_device_setup_set_buffer_size(setup, 256);

    // getOutputChannels() has no C getter; read it off the engine object to
    // prove set_output landed on the right slot.
    CHECK(reinterpret_cast<YSE::deviceSetup*>(setup)->getOutputChannels() == 2);

    yse_device_setup_destroy(setup);
  }

  TEST_CASE("c-api device setup: NULL handles and NULL devices are no-ops") {
    yse_device_setup_destroy(nullptr);
    yse_device_setup_set_input(nullptr, nullptr);
    yse_device_setup_set_output(nullptr, nullptr);
    yse_device_setup_set_sample_rate(nullptr, 44100.0);
    yse_device_setup_set_buffer_size(nullptr, 512);

    YseDeviceSetup* setup = yse_device_setup_create();
    REQUIRE(setup != nullptr);
    // A live setup with a NULL device must leave the previous slot untouched
    // (0 outputs here, because nothing was ever set).
    yse_device_setup_set_input(setup, nullptr);
    yse_device_setup_set_output(setup, nullptr);
    CHECK(reinterpret_cast<YSE::deviceSetup*>(setup)->getOutputChannels() == 0);
    yse_device_setup_destroy(setup);
  }

  // ─── yse_reverb.cpp ────────────────────────────────────────────────────────

  TEST_CASE("c-api reverb: create -> valid -> destroy") {
    if (!ensureOffline()) return;
    YseReverb* rev = yse_reverb_create();
    REQUIRE(rev != nullptr);
    CHECK(yse_reverb_is_valid(rev) == 1);
    yse_reverb_destroy(rev);
    yse_reverb_destroy(nullptr); // destroy(NULL) is a no-op
  }

  TEST_CASE("c-api reverb: zone parameters round-trip and clamp") {
    if (!ensureOffline()) return;
    YseReverb* rev = yse_reverb_create();
    REQUIRE(rev != nullptr);

    const yse_pos_t p{1.5f, -2.f, 3.25f};
    yse_reverb_set_position(rev, &p);
    const yse_pos_t got = yse_reverb_get_position(rev);
    CHECK(got.x == doctest::Approx(1.5f));
    CHECK(got.y == doctest::Approx(-2.f));
    CHECK(got.z == doctest::Approx(3.25f));
    yse_reverb_set_position(rev, nullptr); // NULL position: unchanged
    CHECK(yse_reverb_get_position(rev).x == doctest::Approx(1.5f));

    yse_reverb_set_size(rev, 12.f);
    CHECK(yse_reverb_get_size(rev) == doctest::Approx(12.f));
    yse_reverb_set_size(rev, -1.f); // negatives clamp to zero
    CHECK(yse_reverb_get_size(rev) == doctest::Approx(0.f));

    yse_reverb_set_roll_off(rev, 5.f);
    CHECK(yse_reverb_get_roll_off(rev) == doctest::Approx(5.f));
    yse_reverb_set_roll_off(rev, -3.f);
    CHECK(yse_reverb_get_roll_off(rev) == doctest::Approx(0.f));

    CHECK(yse_reverb_get_active(rev) == 1); // active by default
    yse_reverb_set_active(rev, 0);
    CHECK(yse_reverb_get_active(rev) == 0);
    yse_reverb_set_active(rev, 1);
    CHECK(yse_reverb_get_active(rev) == 1);

    yse_reverb_set_room_size(rev, 2.f); // clamped to [0, 1]
    CHECK(yse_reverb_get_room_size(rev) == doctest::Approx(1.f));
    yse_reverb_set_damping(rev, -1.f);
    CHECK(yse_reverb_get_damping(rev) == doctest::Approx(0.f));

    yse_reverb_set_dry_wet_balance(rev, 0.25f, 0.75f);
    CHECK(yse_reverb_get_dry(rev) == doctest::Approx(0.25f));
    CHECK(yse_reverb_get_wet(rev) == doctest::Approx(0.75f));

    yse_reverb_set_modulation(rev, 2.5f, 15.f);
    CHECK(yse_reverb_get_modulation_frequency(rev) == doctest::Approx(2.5f));
    CHECK(yse_reverb_get_modulation_width(rev) == doctest::Approx(15.f));

    for (int i = 0; i < 4; ++i) {
      yse_reverb_set_reflection(rev, i, 100 * (i + 1), 0.1f * static_cast<float>(i + 1));
      CHECK(yse_reverb_get_reflection_time(rev, i) == 100 * (i + 1));
      CHECK(yse_reverb_get_reflection_gain(rev, i) ==
            doctest::Approx(0.1f * static_cast<float>(i + 1)));
    }

    yse_reverb_destroy(rev);
  }

  TEST_CASE("c-api reverb: presets map onto the engine preset table") {
    if (!ensureOffline()) return;
    YseReverb* rev = yse_reverb_create();
    REQUIRE(rev != nullptr);

    yse_reverb_set_preset(rev, YSE_REVERB_OFF);
    CHECK(yse_reverb_get_room_size(rev) == doctest::Approx(0.f));
    CHECK(yse_reverb_get_damping(rev) == doctest::Approx(0.f));
    CHECK(yse_reverb_get_dry(rev) == doctest::Approx(1.f));
    CHECK(yse_reverb_get_wet(rev) == doctest::Approx(0.f));

    yse_reverb_set_preset(rev, YSE_REVERB_HALL);
    CHECK(yse_reverb_get_room_size(rev) == doctest::Approx(0.7f));
    CHECK(yse_reverb_get_damping(rev) == doctest::Approx(0.4f));
    CHECK(yse_reverb_get_dry(rev) == doctest::Approx(0.5f));
    CHECK(yse_reverb_get_wet(rev) == doctest::Approx(0.5f));

    yse_reverb_set_preset(rev, YSE_REVERB_CAVE);
    CHECK(yse_reverb_get_room_size(rev) == doctest::Approx(1.f));
    CHECK(yse_reverb_get_wet(rev) == doctest::Approx(0.7f));

    // SEWERPIPE is the preset that carries a non-zero modulation pair.
    yse_reverb_set_preset(rev, YSE_REVERB_SEWERPIPE);
    CHECK(yse_reverb_get_modulation_frequency(rev) == doctest::Approx(3.5f));
    CHECK(yse_reverb_get_modulation_width(rev) == doctest::Approx(20.f));

    // Every enum value in the C header must reach the engine table without
    // tripping an assertion or leaving a parameter outside its documented range.
    const YseReverbPreset all[] = {YSE_REVERB_OFF,       YSE_REVERB_GENERIC,   YSE_REVERB_PADDED,
                                   YSE_REVERB_ROOM,      YSE_REVERB_BATHROOM,  YSE_REVERB_STONEROOM,
                                   YSE_REVERB_LARGEROOM, YSE_REVERB_HALL,      YSE_REVERB_CAVE,
                                   YSE_REVERB_SEWERPIPE, YSE_REVERB_UNDERWATER};
    for (YseReverbPreset preset : all) {
      yse_reverb_set_preset(rev, preset);
      CHECK(yse_reverb_get_room_size(rev) >= 0.f);
      CHECK(yse_reverb_get_room_size(rev) <= 1.f);
      CHECK(yse_reverb_get_dry(rev) >= 0.f);
      CHECK(yse_reverb_get_wet(rev) <= 1.f);
    }

    yse_reverb_destroy(rev);
  }

  TEST_CASE("c-api reverb: every entry point is NULL-safe") {
    yse_reverb_set_position(nullptr, nullptr);
    yse_reverb_set_size(nullptr, 1.f);
    yse_reverb_set_roll_off(nullptr, 1.f);
    yse_reverb_set_active(nullptr, 1);
    yse_reverb_set_room_size(nullptr, 1.f);
    yse_reverb_set_damping(nullptr, 1.f);
    yse_reverb_set_dry_wet_balance(nullptr, 0.5f, 0.5f);
    yse_reverb_set_modulation(nullptr, 1.f, 1.f);
    yse_reverb_set_reflection(nullptr, 0, 10, 0.5f);
    yse_reverb_set_preset(nullptr, YSE_REVERB_HALL);

    CHECK(yse_reverb_is_valid(nullptr) == 0);
    const yse_pos_t p = yse_reverb_get_position(nullptr);
    CHECK(p.x == doctest::Approx(0.f));
    CHECK(p.y == doctest::Approx(0.f));
    CHECK(p.z == doctest::Approx(0.f));
    CHECK(yse_reverb_get_size(nullptr) == doctest::Approx(0.f));
    CHECK(yse_reverb_get_roll_off(nullptr) == doctest::Approx(0.f));
    CHECK(yse_reverb_get_active(nullptr) == 0);
    CHECK(yse_reverb_get_room_size(nullptr) == doctest::Approx(0.f));
    CHECK(yse_reverb_get_damping(nullptr) == doctest::Approx(0.f));
    CHECK(yse_reverb_get_dry(nullptr) == doctest::Approx(0.f));
    CHECK(yse_reverb_get_wet(nullptr) == doctest::Approx(0.f));
    CHECK(yse_reverb_get_modulation_frequency(nullptr) == doctest::Approx(0.f));
    CHECK(yse_reverb_get_modulation_width(nullptr) == doctest::Approx(0.f));
    CHECK(yse_reverb_get_reflection_time(nullptr, 0) == 0);
    CHECK(yse_reverb_get_reflection_gain(nullptr, 0) == doctest::Approx(0.f));
  }

  TEST_CASE("c-api reverb: the borrowed global-reverb handle is live") {
    if (!ensureOffline()) return;
    YseReverb* global = yse_system_get_global_reverb(yse_system_get());
    REQUIRE(global != nullptr);
    CHECK(yse_reverb_is_valid(global) == 1);
    // Borrowed handle — never destroyed here (see yse_c_internal.hpp).
  }

  // ─── yse_log.cpp ───────────────────────────────────────────────────────────

  TEST_CASE("c-api log: the singleton handle is stable and the level round-trips") {
    LogStateGuard guard;
    YseLog* log = yse_log_get();
    REQUIRE(log != nullptr);
    CHECK(log == yse_log_get());

    const YseErrorLevel levels[] = {YSE_EL_NONE, YSE_EL_ERROR, YSE_EL_WARNING, YSE_EL_DEBUG};
    for (YseErrorLevel level : levels) {
      yse_log_set_level(log, level);
      CHECK(yse_log_get_level(log) == level);
    }
  }

  TEST_CASE("c-api log: an installed callback receives emitted messages") {
    LogStateGuard guard;
    YseLog* log = yse_log_get();
    LogSink sink;

    yse_log_set_level(log, YSE_EL_DEBUG);
    yse_log_set_callback(log, &logReceive, &sink);
    yse_log_send_message(log, "capisurface-hello");
    CHECK(sink.countMatching("capisurface-hello") == 1);
    // The engine tags application messages before handing them to the sink.
    CHECK(sink.countMatching("(App Message)") == 1);

    // A NULL message is dropped rather than forwarded.
    yse_log_send_message(log, nullptr);
    CHECK(sink.countMatching("capisurface-hello") == 1);

    // Passing NULL for cb restores the default file sink, so nothing further
    // reaches this callback.
    yse_log_set_callback(log, nullptr, nullptr);
    yse_log_send_message(log, "capisurface-after-detach");
    CHECK(sink.countMatching("capisurface-after-detach") == 0);
  }

  TEST_CASE("c-api log: level EL_NONE drops messages") {
    LogStateGuard guard;
    YseLog* log = yse_log_get();
    LogSink sink;

    yse_log_set_callback(log, &logReceive, &sink);
    yse_log_set_level(log, YSE_EL_NONE);
    yse_log_send_message(log, "capisurface-silenced");
    CHECK(sink.countMatching("capisurface-silenced") == 0);

    yse_log_set_level(log, YSE_EL_ERROR);
    yse_log_send_message(log, "capisurface-audible");
    CHECK(sink.countMatching("capisurface-audible") == 1);
  }

  TEST_CASE("c-api log: get_logfile follows the snprintf convention") {
    LogStateGuard guard;
    YseLog* log = yse_log_get();

    yse_log_set_logfile(log, "capisurface_log.txt"); // 19 chars
    CHECK(yse_log_get_logfile(log, nullptr, 0) == 19); // size query
    CHECK(yse_log_get_logfile(log, nullptr, 32) == 19);

    char exact[20] = {0};
    CHECK(yse_log_get_logfile(log, exact, sizeof(exact)) == 19);
    CHECK(std::string(exact) == "capisurface_log.txt");

    char small[6] = {0};
    CHECK(yse_log_get_logfile(log, small, sizeof(small)) == 19);
    CHECK(std::string(small) == "capis");
    CHECK(small[5] == '\0');

    char probe = '@';
    CHECK(yse_log_get_logfile(log, &probe, 0) == 19); // cap == 0 must not write
    CHECK(probe == '@');

    yse_log_set_logfile(log, nullptr); // NULL path is a no-op
    CHECK(yse_log_get_logfile(log, exact, sizeof(exact)) == 19);
    CHECK(std::string(exact) == "capisurface_log.txt");
  }

  TEST_CASE("c-api log: every entry point is NULL-safe") {
    char buf[8];
    std::memset(buf, 'x', sizeof(buf));
    CHECK(yse_log_get_logfile(nullptr, buf, sizeof(buf)) == 0);
    CHECK(buf[0] == '\0');
    CHECK(yse_log_get_logfile(nullptr, nullptr, 0) == 0);

    CHECK(yse_log_get_level(nullptr) == YSE_EL_NONE);
    yse_log_set_level(nullptr, YSE_EL_DEBUG);
    yse_log_send_message(nullptr, "ignored");
    yse_log_set_logfile(nullptr, "ignored.txt");
    yse_log_set_callback(nullptr, &logReceive, nullptr);
    yse_log_free_message(nullptr);

    // The level of the real singleton must be untouched by the NULL setter.
    CHECK(yse_log_get_level(yse_log_get()) != YSE_EL_NONE);
  }

  // ─── yse_listener.cpp ──────────────────────────────────────────────────────

  TEST_CASE("c-api listener: the singleton handle is stable and position round-trips") {
    YseListener* l = yse_listener_get();
    REQUIRE(l != nullptr);
    CHECK(l == yse_listener_get());

    const yse_pos_t p{1.f, -2.5f, 3.f};
    yse_listener_set_pos(l, &p);
    const yse_pos_t got = yse_listener_get_pos(l);
    CHECK(got.x == doctest::Approx(1.f));
    CHECK(got.y == doctest::Approx(-2.5f));
    CHECK(got.z == doctest::Approx(3.f));

    yse_listener_set_pos(l, nullptr); // NULL position is a no-op
    CHECK(yse_listener_get_pos(l).x == doctest::Approx(1.f));

    const yse_pos_t origin{0.f, 0.f, 0.f};
    yse_listener_set_pos(l, &origin);
  }

  TEST_CASE("c-api listener: orientation round-trips through both vectors") {
    YseListener* l = yse_listener_get();
    const yse_pos_t forward{0.f, 0.f, 1.f};
    const yse_pos_t up{0.f, 1.f, 0.f};

    yse_listener_set_orient(l, &forward, &up);
    const yse_pos_t f = yse_listener_get_forward(l);
    const yse_pos_t u = yse_listener_get_upward(l);
    CHECK(f.x == doctest::Approx(0.f));
    CHECK(f.z == doctest::Approx(1.f));
    CHECK(u.y == doctest::Approx(1.f));

    // Either vector NULL leaves both untouched.
    const yse_pos_t other{1.f, 0.f, 0.f};
    yse_listener_set_orient(l, &other, nullptr);
    yse_listener_set_orient(l, nullptr, &other);
    CHECK(yse_listener_get_forward(l).z == doctest::Approx(1.f));
    CHECK(yse_listener_get_upward(l).y == doctest::Approx(1.f));

    // Velocity is engine-maintained; the C getter must simply read it back as a
    // finite vector rather than fail.
    const yse_pos_t v = yse_listener_get_vel(l);
    CHECK(v.x == v.x); // not NaN
    CHECK(v.y == v.y);
    CHECK(v.z == v.z);
  }

  TEST_CASE("c-api listener: a NULL handle yields the zero vector") {
    yse_listener_set_pos(nullptr, nullptr);
    yse_listener_set_orient(nullptr, nullptr, nullptr);

    const yse_pos_t zeros[] = {yse_listener_get_pos(nullptr), yse_listener_get_vel(nullptr),
                               yse_listener_get_forward(nullptr), yse_listener_get_upward(nullptr)};
    for (const yse_pos_t& z : zeros) {
      CHECK(z.x == doctest::Approx(0.f));
      CHECK(z.y == doctest::Approx(0.f));
      CHECK(z.z == doctest::Approx(0.f));
    }
  }

  // ─── yse_buffer_io.cpp ─────────────────────────────────────────────────────
  //
  // BufferIO keeps its registry in a process-global that the destructor frees,
  // so only one instance may be alive at a time — each case creates exactly one
  // and destroys it before returning (same constraint the C++ io suite documents).

  TEST_CASE("c-api buffer io: create/destroy, active toggle and registration") {
    YseBufferIO* io = yse_buffer_io_create(0);
    REQUIRE(io != nullptr);

    CHECK(yse_buffer_io_get_active(io) == 0);
    yse_buffer_io_set_active(io, 1);
    CHECK(yse_buffer_io_get_active(io) == 1);

    char data[16] = {0};
    CHECK(yse_buffer_io_name_exists(io, "capi-buf") == 0);
    CHECK(yse_buffer_io_add(io, "capi-buf", data, static_cast<int>(sizeof(data))) == 1);
    CHECK(yse_buffer_io_name_exists(io, "capi-buf") == 1);
    // Duplicate IDs are rejected.
    CHECK(yse_buffer_io_add(io, "capi-buf", data, static_cast<int>(sizeof(data))) == 0);

    CHECK(yse_buffer_io_remove_by_name(io, "capi-buf") == 1);
    CHECK(yse_buffer_io_name_exists(io, "capi-buf") == 0);
    CHECK(yse_buffer_io_remove_by_name(io, "capi-buf") == 0);

    yse_buffer_io_set_active(io, 0);
    CHECK(yse_buffer_io_get_active(io) == 0);
    yse_buffer_io_destroy(io);
    yse_buffer_io_destroy(nullptr); // destroy(NULL) is a no-op
  }

  TEST_CASE("c-api buffer io: store_copy keeps the bytes alive past the caller's buffer") {
    YseBufferIO* io = yse_buffer_io_create(1);
    REQUIRE(io != nullptr);
    yse_buffer_io_set_active(io, 1);
    {
      char scoped[8] = {1, 2, 3, 4, 5, 6, 7, 8};
      REQUIRE(yse_buffer_io_add(io, "capi-copy", scoped, static_cast<int>(sizeof(scoped))) == 1);
    } // `scoped` dies here; the owned copy must survive
    CHECK(yse_buffer_io_name_exists(io, "capi-copy") == 1);
    CHECK(yse_buffer_io_remove_by_name(io, "capi-copy") == 1);
    yse_buffer_io_set_active(io, 0);
    yse_buffer_io_destroy(io);
  }

  TEST_CASE("c-api buffer io: invalid arguments and NULL handles return zero") {
    char data[4] = {0};

    CHECK(yse_buffer_io_get_active(nullptr) == 0);
    yse_buffer_io_set_active(nullptr, 1);
    CHECK(yse_buffer_io_name_exists(nullptr, "x") == 0);
    CHECK(yse_buffer_io_add(nullptr, "x", data, 4) == 0);
    CHECK(yse_buffer_io_remove_by_name(nullptr, "x") == 0);

    YseBufferIO* io = yse_buffer_io_create(0);
    REQUIRE(io != nullptr);
    CHECK(yse_buffer_io_name_exists(io, nullptr) == 0);
    CHECK(yse_buffer_io_add(io, nullptr, data, 4) == 0); // NULL id
    CHECK(yse_buffer_io_add(io, "x", nullptr, 4) == 0); // NULL buffer
    CHECK(yse_buffer_io_add(io, "x", data, 0) == 0); // zero length
    CHECK(yse_buffer_io_add(io, "x", data, -1) == 0); // negative length
    CHECK(yse_buffer_io_remove_by_name(io, nullptr) == 0);
    CHECK(yse_buffer_io_name_exists(io, "x") == 0); // nothing was registered
    yse_buffer_io_destroy(io);
  }

} // TEST_SUITE("capisurface")
