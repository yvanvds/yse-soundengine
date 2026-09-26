// C-API host bus tap tests (issue #389) — exercises YseEngine/c_api/yse_bus.*
// through the flat C ABI: yse_bus_tap_create / yse_bus_tap_destroy plus the
// yse_bus_tap_cb frame contract (five payload kinds incl. bang, engine-owned
// buffers valid only for the call).
//
// Publishes are driven through INTERNAL::Bus() directly (the tests link
// yse_objects with full symbol access) — there is no C publish surface, and
// the tap's job is to observe engine-side publishes.
//
// The engine runs offline (yse_system_init_offline), so no audio hardware is
// needed and the suite runs in CI. Because it calls yse_system_close() — the
// tap-invalidation contract is part of the surface under test — it lives in
// its own TEST_SUITE("buscapi") and its own ctest process (see
// Tests/CMakeLists.txt), like the other lifecycle-driving c-api suites
// (#298/#304 isolation).

#include <doctest/doctest.h>

#include <string>
#include <thread>
#include <vector>

#include "yse_c/yse_bus.h"
#include "yse_c/yse_channel.h"
#include "yse_c/yse_dsp.h"
#include "yse_c/yse_patcher.h"
#include "yse_c/yse_sound.h"
#include "yse_c/yse_system.h"

#include "yse.hpp"
#include "internal/namedBus.h"

namespace {

  // One decoded callback frame, with the engine-owned buffers copied during
  // the call — exactly what the header tells a real host to do.
  struct Frame {
    std::string address;
    YseBusValueKind kind{YSE_BUS_BANG};
    int i = 0;
    float f = 0.0f;
    std::string str;
    std::vector<float> list;
    bool strWasNull = true;
    bool listWasNull = true;
  };

  struct Sink {
    std::vector<Frame> frames;
  };

  void YSE_C_CALLBACK captureCb(const char* address, YseBusValueKind kind, int i, float f,
                                const char* str, const float* list, size_t list_len,
                                void* user_data) {
    auto* sink = static_cast<Sink*>(user_data);
    Frame frame;
    frame.address = address != nullptr ? address : "";
    frame.kind = kind;
    frame.i = i;
    frame.f = f;
    frame.strWasNull = str == nullptr;
    if (str != nullptr) frame.str = str;
    frame.listWasNull = list == nullptr;
    if (list != nullptr) frame.list.assign(list, list + list_len);
    sink->frames.push_back(frame);
  }

  void publish(const char* name, YSE::INTERNAL::BusValue value) {
    // The test thread ran init_offline, so it is the control thread: a T_GUI
    // publish dispatches synchronously and the tap fires before this returns.
    YSE::INTERNAL::Bus().publish(name, value, YSE::T_GUI);
  }

} // namespace

TEST_SUITE("buscapi") {

  // The engine-down contract. It normalizes rather than assuming it runs
  // first: doctest orders cases by file, so in a process shared with other
  // suites an earlier one's session can still be up here (issue #715). close()
  // is a no-op on an inactive engine, so the isolated run is unchanged.
  TEST_CASE("c-api bus: create fails cleanly before init and on NULL args") {
    yse_system_close(yse_system_get());
    yse_clear_last_error();
    CHECK(yse_bus_tap_create("phi.ctl.", &captureCb, nullptr) == nullptr);
    CHECK(std::string(yse_last_error()).find("not initialised") != std::string::npos);

    CHECK(yse_bus_tap_create(nullptr, &captureCb, nullptr) == nullptr);
    CHECK(yse_bus_tap_create("phi.ctl.", nullptr, nullptr) == nullptr);

    yse_bus_tap_destroy(nullptr); // null-safe no-op, engine down or not

    // Publish / subscribe (#904): engine down is a reported error, not a crash.
    yse_clear_last_error();
    CHECK(yse_bus_publish_int("cap.x", 1) == YSE_ERR_NOT_INITIALIZED);
    CHECK(std::string(yse_last_error()).find("not initialised") != std::string::npos);
    CHECK(yse_bus_publish_bang("cap.x") == YSE_ERR_NOT_INITIALIZED);
    CHECK(yse_bus_subscribe("cap.x", &captureCb, nullptr) == nullptr);
    yse_bus_unsubscribe(nullptr); // null-safe no-op
  }

  TEST_CASE("c-api bus: tap delivers all five value kinds with call-time copies") {
    YseSystem* sys = yse_system_get();
    REQUIRE(yse_system_init_offline(sys) == YSE_OK);

    Sink sink;
    YseBusTap* tap = yse_bus_tap_create("cap.k.", &captureCb, &sink);
    REQUIRE(tap != nullptr);

    publish("cap.k.bang", YSE::INTERNAL::BusValue{});
    publish("cap.k.int", YSE::INTERNAL::BusValue{42});
    publish("cap.k.float", YSE::INTERNAL::BusValue{1.5f});
    publish("cap.k.str", YSE::INTERNAL::BusValue{std::string("hello")});
    publish("cap.k.list", YSE::INTERNAL::BusValue{std::vector<float>{3.0f, 4.0f, 5.0f}});

    REQUIRE(sink.frames.size() == 5);

    CHECK(sink.frames[0].address == "cap.k.bang");
    CHECK(sink.frames[0].kind == YSE_BUS_BANG);
    CHECK(sink.frames[0].strWasNull);
    CHECK(sink.frames[0].listWasNull);

    CHECK(sink.frames[1].address == "cap.k.int");
    CHECK(sink.frames[1].kind == YSE_BUS_INT);
    CHECK(sink.frames[1].i == 42);

    CHECK(sink.frames[2].address == "cap.k.float");
    CHECK(sink.frames[2].kind == YSE_BUS_FLOAT);
    CHECK(sink.frames[2].f == doctest::Approx(1.5f));

    CHECK(sink.frames[3].address == "cap.k.str");
    CHECK(sink.frames[3].kind == YSE_BUS_STRING);
    CHECK_FALSE(sink.frames[3].strWasNull);
    CHECK(sink.frames[3].str == "hello");

    CHECK(sink.frames[4].address == "cap.k.list");
    CHECK(sink.frames[4].kind == YSE_BUS_LIST);
    REQUIRE(sink.frames[4].list.size() == 3);
    CHECK(sink.frames[4].list[0] == doctest::Approx(3.0f));
    CHECK(sink.frames[4].list[2] == doctest::Approx(5.0f));

    yse_bus_tap_destroy(tap);
  }

  TEST_CASE("c-api bus: multiple taps filter by their own prefix and user_data") {
    Sink ctl, all;
    YseBusTap* tapCtl = yse_bus_tap_create("cap.ctl.", &captureCb, &ctl);
    YseBusTap* tapAll = yse_bus_tap_create("", &captureCb, &all); // matches everything
    REQUIRE(tapCtl != nullptr);
    REQUIRE(tapAll != nullptr);

    publish("cap.ctl.play", YSE::INTERNAL::BusValue{1});
    publish("cap.other", YSE::INTERNAL::BusValue{2});

    REQUIRE(ctl.frames.size() == 1);
    CHECK(ctl.frames[0].address == "cap.ctl.play");
    CHECK(all.frames.size() == 2);

    yse_bus_tap_destroy(tapCtl);
    publish("cap.ctl.stop", YSE::INTERNAL::BusValue{3});
    CHECK(ctl.frames.size() == 1); // destroyed — no further delivery
    CHECK(all.frames.size() == 3);

    yse_bus_tap_destroy(tapAll);
  }

  TEST_CASE("c-api bus: audio-thread publishes arrive on the next yse_system_update") {
    YseSystem* sys = yse_system_get();

    Sink sink;
    YseBusTap* tap = yse_bus_tap_create("cap.dsp.", &captureCb, &sink);
    REQUIRE(tap != nullptr);

    YSE::INTERNAL::Bus().publish("cap.dsp.meter", YSE::INTERNAL::BusValue{0.25f}, YSE::T_DSP);
    CHECK(sink.frames.empty()); // queued, not delivered inline

    yse_system_update(sys); // drainPending() runs on this (control) thread
    REQUIRE(sink.frames.size() == 1);
    CHECK(sink.frames[0].address == "cap.dsp.meter");
    CHECK(sink.frames[0].kind == YSE_BUS_FLOAT);
    CHECK(sink.frames[0].f == doctest::Approx(0.25f));

    yse_bus_tap_destroy(tap);
  }

  TEST_CASE("c-api bus: publish rejects bad arguments with a last error (#904)") {
    yse_clear_last_error();
    CHECK(yse_bus_publish_int(nullptr, 1) == YSE_ERR_INVALID_ARGUMENT);
    CHECK(std::string(yse_last_error()).find("address") != std::string::npos);
    CHECK(yse_bus_publish_float("", 1.0f) == YSE_ERR_INVALID_ARGUMENT);
    yse_clear_last_error();
    CHECK(yse_bus_publish_string("cap.x", nullptr) == YSE_ERR_INVALID_ARGUMENT);
    CHECK(std::string(yse_last_error()).find("value") != std::string::npos);
    yse_clear_last_error();
    CHECK(yse_bus_publish_list("cap.x", nullptr, 3) == YSE_ERR_INVALID_ARGUMENT);
    CHECK(std::string(yse_last_error()).find("NULL") != std::string::npos);

    CHECK(yse_bus_subscribe(nullptr, &captureCb, nullptr) == nullptr);
    CHECK(yse_bus_subscribe("", &captureCb, nullptr) == nullptr);
    CHECK(yse_bus_subscribe("cap.x", nullptr, nullptr) == nullptr);
  }

  TEST_CASE("c-api bus: publish delivers all five kinds to an exact subscriber and taps (#904)") {
    Sink exact, tap;
    YseBusSub* sub = yse_bus_subscribe("cap.pub.v", &captureCb, &exact);
    YseBusTap* t = yse_bus_tap_create("cap.pub.", &captureCb, &tap);
    REQUIRE(sub != nullptr);
    REQUIRE(t != nullptr);

    const float values[] = {3.0f, 4.0f, 5.0f};
    // Control-thread publishes dispatch before the call returns.
    CHECK(yse_bus_publish_bang("cap.pub.v") == YSE_OK);
    CHECK(yse_bus_publish_int("cap.pub.v", 42) == YSE_OK);
    CHECK(yse_bus_publish_float("cap.pub.v", 1.5f) == YSE_OK);
    CHECK(yse_bus_publish_string("cap.pub.v", "hello") == YSE_OK);
    CHECK(yse_bus_publish_list("cap.pub.v", values, 3) == YSE_OK);
    CHECK(yse_bus_publish_list("cap.pub.v", nullptr, 0) == YSE_OK); // empty list

    REQUIRE(exact.frames.size() == 6);
    CHECK(tap.frames.size() == 6);
    for (const Frame& fr : exact.frames)
      CHECK(fr.address == "cap.pub.v");
    CHECK(exact.frames[0].kind == YSE_BUS_BANG);
    CHECK(exact.frames[1].kind == YSE_BUS_INT);
    CHECK(exact.frames[1].i == 42);
    CHECK(exact.frames[2].kind == YSE_BUS_FLOAT);
    CHECK(exact.frames[2].f == doctest::Approx(1.5f));
    CHECK(exact.frames[3].kind == YSE_BUS_STRING);
    CHECK(exact.frames[3].str == "hello");
    CHECK(exact.frames[4].kind == YSE_BUS_LIST);
    REQUIRE(exact.frames[4].list.size() == 3);
    CHECK(exact.frames[4].list[1] == doctest::Approx(4.0f));
    CHECK(exact.frames[5].kind == YSE_BUS_LIST);
    CHECK(exact.frames[5].list.empty());

    // Exact match: a longer address that a same-string tap would see does not
    // reach the subscription.
    CHECK(yse_bus_publish_int("cap.pub.v2", 7) == YSE_OK);
    CHECK(exact.frames.size() == 6);
    CHECK(tap.frames.size() == 7);

    // Unsubscribed — no further delivery; a publish no one hears is still OK.
    yse_bus_unsubscribe(sub);
    CHECK(yse_bus_publish_int("cap.pub.v", 8) == YSE_OK);
    CHECK(exact.frames.size() == 6);
    yse_bus_tap_destroy(t);
    CHECK(yse_bus_publish_int("cap.pub.v", 9) == YSE_OK);
  }

  TEST_CASE("c-api bus: off-control-thread publish arrives on the next update (#904)") {
    YseSystem* sys = yse_system_get();
    Sink sink;
    YseBusSub* sub = yse_bus_subscribe("cap.thr.s", &captureCb, &sink);
    REQUIRE(sub != nullptr);

    YseStatus st = YSE_ERR_GENERIC;
    std::thread producer([&st] { st = yse_bus_publish_string("cap.thr.s", "from host thread"); });
    producer.join();
    CHECK(st == YSE_OK);
    CHECK(sink.frames.empty()); // parked in the control inbox, not dispatched inline

    yse_system_update(sys);
    REQUIRE(sink.frames.size() == 1);
    CHECK(sink.frames[0].kind == YSE_BUS_STRING);
    CHECK(sink.frames[0].str == "from host thread");

    yse_bus_unsubscribe(sub);
  }

  // The user-visible flow the issue is for: a C host drives a named patcher's
  // .r and hears the patcher's .s answer, with no Python in the loop.
  TEST_CASE("c-api bus: host publish drives a named patcher .r; exact subscribe hears its .s "
            "(#904)") {
    YsePatcher* p = yse_patcher_create();
    REQUIRE(p != nullptr);
    REQUIRE(yse_patcher_set_name(p, "capi904") == YSE_OK);
    yse_patcher_init(p, 2);
    YsePHandle* recv = yse_patcher_create_object(p, ".r", "in");
    YsePHandle* send = yse_patcher_create_object(p, ".s", "out");
    REQUIRE(recv != nullptr);
    REQUIRE(send != nullptr);
    yse_patcher_connect(p, recv, 0, send, 0);

    Sink out;
    YseBusSub* sub = yse_bus_subscribe("patcher.capi904.out", &captureCb, &out);
    REQUIRE(sub != nullptr);

    CHECK(yse_bus_publish_int("patcher.capi904.in", 17) == YSE_OK);
    yse_system_update(yse_system_get());
    REQUIRE(out.frames.size() == 1);
    CHECK(out.frames[0].address == "patcher.capi904.out");
    CHECK(out.frames[0].kind == YSE_BUS_INT);
    CHECK(out.frames[0].i == 17);

    CHECK(yse_bus_publish_float("patcher.capi904.in", 0.5f) == YSE_OK);
    yse_system_update(yse_system_get());
    REQUIRE(out.frames.size() == 2);
    CHECK(out.frames[1].kind == YSE_BUS_FLOAT);
    CHECK(out.frames[1].f == doctest::Approx(0.5f));

    yse_bus_unsubscribe(sub);
    yse_patcher_destroy(p);
  }

  // #905: a C host names a channel, then drives its volume by bus address
  // with yse_bus_publish_* — no C++ handle in the loop.
  TEST_CASE("c-api bus: yse_channel_set_name makes channel.<name>.volume addressable (#905)") {
    YseChannel* a = yse_channel_create("cap905.a.log", yse_channel_master());
    YseChannel* b = yse_channel_create("cap905.b.log", yse_channel_master());
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    yse_system_update(yse_system_get());
    REQUIRE(yse_channel_is_valid(a) == 1);
    yse_channel_set_volume(a, 1.0f);
    yse_channel_set_volume(b, 1.0f);

    yse_channel_set_name(a, "cap905ch");
    CHECK(yse_bus_publish_float("channel.cap905ch.volume", 0.25f) == YSE_OK);
    CHECK(yse_channel_get_volume(a) == doctest::Approx(0.25f));
    CHECK(yse_bus_publish_int("channel.cap905ch.volume", 0) == YSE_OK);
    CHECK(yse_channel_get_volume(a) == doctest::Approx(0.0f));

    // The bus name is independent of the log name, which the getter reports.
    char buf[64];
    CHECK(yse_channel_get_name(a, buf, sizeof(buf)) == std::string("cap905.a.log").size());
    CHECK(std::string(buf) == "cap905.a.log");

    // Duplicate: rejected engine-side, first registration keeps the address.
    yse_channel_set_name(b, "cap905ch");
    CHECK(yse_bus_publish_float("channel.cap905ch.volume", 0.5f) == YSE_OK);
    CHECK(yse_channel_get_volume(a) == doctest::Approx(0.5f));
    CHECK(yse_channel_get_volume(b) == doctest::Approx(1.0f));

    // NULL clears: the address goes dead and the name is free to reclaim.
    yse_channel_set_name(a, nullptr);
    CHECK(yse_bus_publish_float("channel.cap905ch.volume", 0.75f) == YSE_OK);
    CHECK(yse_channel_get_volume(a) == doctest::Approx(0.5f));
    yse_channel_set_name(b, ""); // b holds no claim; clearing is harmless
    yse_channel_set_name(b, "cap905ch"); // now free — b takes it
    CHECK(yse_bus_publish_float("channel.cap905ch.volume", 0.75f) == YSE_OK);
    CHECK(yse_channel_get_volume(b) == doctest::Approx(0.75f));

    yse_channel_destroy(b);
    yse_channel_destroy(a);
    yse_system_update(yse_system_get());
  }

  TEST_CASE("c-api bus: yse_sound_set_name makes sound.<name>.* addressable (#905)") {
    const unsigned int len = 1024;
    YseDspBuffer* buf = yse_dsp_buffer_create(len, 0);
    REQUIRE(buf != nullptr);
    std::vector<float> tone(len, 0.25f);
    REQUIRE(yse_dsp_buffer_write(buf, 0, tone.data(), len) == len);

    YseSound* s = yse_sound_create();
    YseSound* dup = yse_sound_create();
    REQUIRE(s != nullptr);
    REQUIRE(dup != nullptr);
    REQUIRE(yse_sound_load_buffer(s, buf, yse_channel_master(), 1, 1.0f) == YSE_OK);
    REQUIRE(yse_sound_load_buffer(dup, buf, yse_channel_master(), 1, 1.0f) == YSE_OK);
    REQUIRE(yse_sound_is_valid(s) == 1);

    yse_sound_set_name(s, "cap905snd");
    CHECK(yse_bus_publish_float("sound.cap905snd.volume", 0.25f) == YSE_OK);
    CHECK(yse_sound_get_volume(s) == doctest::Approx(0.25f));
    CHECK(yse_bus_publish_float("sound.cap905snd.speed", 2.0f) == YSE_OK);
    CHECK(yse_sound_get_speed(s) == doctest::Approx(2.0f));
    const float where[] = {1.0f, 2.0f, 3.0f};
    CHECK(yse_bus_publish_list("sound.cap905snd.position", where, 3) == YSE_OK);
    const yse_pos_t p = yse_sound_get_pos(s);
    CHECK(p.x == doctest::Approx(1.0f));
    CHECK(p.y == doctest::Approx(2.0f));
    CHECK(p.z == doctest::Approx(3.0f));

    // Duplicate is rejected; the first sound keeps the address.
    yse_sound_set_name(dup, "cap905snd");
    CHECK(yse_bus_publish_float("sound.cap905snd.volume", 0.5f) == YSE_OK);
    CHECK(yse_sound_get_volume(s) == doctest::Approx(0.5f));
    CHECK(yse_sound_get_volume(dup) == doctest::Approx(1.0f));

    // NULL clears the name and its subscriptions.
    yse_sound_set_name(s, nullptr);
    CHECK(yse_bus_publish_float("sound.cap905snd.volume", 0.75f) == YSE_OK);
    CHECK(yse_sound_get_volume(s) == doctest::Approx(0.5f));

    yse_sound_destroy(dup);
    yse_sound_destroy(s);
    yse_system_update(yse_system_get());
    yse_dsp_buffer_destroy(buf);
  }

  TEST_CASE("c-api bus: yse_system_close invalidates taps; hosts re-create after re-init") {
    YseSystem* sys = yse_system_get();

    Sink stale;
    YseBusTap* staleTap = yse_bus_tap_create("cap.life.", &captureCb, &stale);
    REQUIRE(staleTap != nullptr);

    yse_system_close(sys);

    // Engine down: create must fail, destroy of the stale tap must be safe.
    CHECK(yse_bus_tap_create("cap.life.", &captureCb, &stale) == nullptr);

    REQUIRE(yse_system_init_offline(sys) == YSE_OK);

    // The stale tap did not survive the restart: a matching publish on the
    // new session's bus reaches only a freshly created tap.
    Sink fresh;
    YseBusTap* freshTap = yse_bus_tap_create("cap.life.", &captureCb, &fresh);
    REQUIRE(freshTap != nullptr);

    publish("cap.life.tick", YSE::INTERNAL::BusValue{1});
    CHECK(stale.frames.empty());
    CHECK(fresh.frames.size() == 1);

    // Destroying the stale handle now (engine active again) must not disturb
    // the new registration — tap handles are process-unique across sessions.
    yse_bus_tap_destroy(staleTap);
    publish("cap.life.tock", YSE::INTERNAL::BusValue{2});
    CHECK(fresh.frames.size() == 2);

    // Exact subscriptions (#904) follow the same close/re-init contract.
    Sink staleSub;
    YseBusSub* sub = yse_bus_subscribe("cap.life.sub", &captureCb, &staleSub);
    REQUIRE(sub != nullptr);
    yse_system_close(sys);
    REQUIRE(yse_system_init_offline(sys) == YSE_OK);
    Sink freshSub;
    YseBusSub* sub2 = yse_bus_subscribe("cap.life.sub", &captureCb, &freshSub);
    REQUIRE(sub2 != nullptr);
    yse_bus_unsubscribe(sub); // stale handle — must not drop sub2
    CHECK(yse_bus_publish_int("cap.life.sub", 5) == YSE_OK);
    CHECK(staleSub.frames.empty());
    CHECK(freshSub.frames.size() == 1);
    yse_bus_unsubscribe(sub2);

    yse_bus_tap_destroy(freshTap); // stale since the second close — safe no-op
    yse_system_close(sys);
  }

} // TEST_SUITE("buscapi")
