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
#include <vector>

#include "yse_c/yse_bus.h"
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

  // Runs first (doctest keeps file order): no engine session exists yet.
  TEST_CASE("c-api bus: create fails cleanly before init and on NULL args") {
    yse_clear_last_error();
    CHECK(yse_bus_tap_create("phi.ctl.", &captureCb, nullptr) == nullptr);
    CHECK(std::string(yse_last_error()).find("not initialised") != std::string::npos);

    CHECK(yse_bus_tap_create(nullptr, &captureCb, nullptr) == nullptr);
    CHECK(yse_bus_tap_create("phi.ctl.", nullptr, nullptr) == nullptr);

    yse_bus_tap_destroy(nullptr); // null-safe no-op, engine down or not
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

    yse_bus_tap_destroy(freshTap);
    yse_system_close(sys);
  }

} // TEST_SUITE("buscapi")
