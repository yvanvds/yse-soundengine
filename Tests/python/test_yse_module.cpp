// Tests for the `yse` live-coding module (issue #126, epic #119).
//
// Only built when YSE_ENABLE_PYTHON is ON (added conditionally in
// Tests/CMakeLists.txt); the macro guard is belt-and-suspenders.
//
// These drive the real script path: source is submitted through the C API
// (yse_run_script), executed asynchronously on the engine's script thread, and
// observed two ways —
//   * C++ bus subscribers, since yse.send dispatches T_GUI synchronously; and
//   * the #125 error callback, for type errors and raising handlers.
// The script thread is asynchronous, so every assertion polls through
// System().update() (which drains the bus, advances the DSL tick, wakes the
// worker, and drains results) rather than assuming a single tick suffices.
//
// Engine-dependent like test_c_api_python.cpp: each case guards on
// engineInit() and skips gracefully on CI when no audio device is available.
// All registry state is process-global, so cases use unique bus names and tear
// down their own C++ subscriptions to avoid cross-talk.

#if YSE_ENABLE_PYTHON

#include <doctest/doctest.h>

#include <mutex>
#include <string>
#include <vector>

#include "yse.hpp"
#include "yse_c/yse_python.h"
#include "internal/namedBus.h"
#include "support/null_device.hpp"

namespace {

  using YSE::INTERNAL::BusValue;
  using YSE::INTERNAL::SubHandle;

  YSE::INTERNAL::NamedBus& bus() {
    return YSE::INTERNAL::Bus();
  }

  // Thread-safe sink for bus values: the subscription fires on the script thread
  // (T_GUI synchronous dispatch), the test reads from its own thread.
  struct BusCapture {
    std::mutex m;
    std::vector<BusValue> values;

    void push(const BusValue& v) {
      std::lock_guard<std::mutex> lock(m);
      values.push_back(v);
    }
    std::size_t count() {
      std::lock_guard<std::mutex> lock(m);
      return values.size();
    }
    BusValue last() {
      std::lock_guard<std::mutex> lock(m);
      return values.back();
    }
  };

  // RAII bus subscription that tears itself down at end of scope, so a later
  // publish to the same name can never reach a destroyed BusCapture.
  struct ScopedSub {
    SubHandle handle;
    ScopedSub(const std::string& name, BusCapture& cap) {
      handle = bus().subscribe(name, [&cap](const BusValue& v) { cap.push(v); });
    }
    ~ScopedSub() {
      bus().unsubscribe(handle);
    }
    ScopedSub(const ScopedSub&) = delete;
    ScopedSub& operator=(const ScopedSub&) = delete;
  };

  // Pump update() until `pred()` holds or the budget is exhausted.
  template <typename Pred> bool pumpUntil(Pred pred, int tries = 300, unsigned ms = 10) {
    for (int i = 0; i < tries; ++i) {
      if (pred()) return true;
      YSE::System().update();
      YSE::System().sleep(ms);
    }
    return pred();
  }

  bool pumpCount(BusCapture& cap, std::size_t n) {
    return pumpUntil([&] { return cap.count() >= n; });
  }

  void run(const char* src) {
    yse_run_script(src);
  }

  // Error-callback sink, mirroring test_c_api_python.cpp.
  struct ErrSink {
    int count = 0;
    std::string last;
  };
  void YSE_C_CALLBACK captureErr(const char* tb, void* ud) {
    auto* s = static_cast<ErrSink*>(ud);
    s->count++;
    s->last = tb != nullptr ? tb : "";
  }

} // namespace

TEST_SUITE("python") {

  TEST_CASE("yse module: send marshals each value type to the bus") {
    if (!TestHelpers::engineInit()) return;

    BusCapture iCap, fCap, sCap, lCap;
    ScopedSub iSub("ysemod.int", iCap);
    ScopedSub fSub("ysemod.float", fCap);
    ScopedSub sSub("ysemod.str", sCap);
    ScopedSub lSub("ysemod.list", lCap);

    run("yse.send('ysemod.int', 5)\n"
        "yse.send('ysemod.float', 0.5)\n"
        "yse.send('ysemod.str', 'hi')\n"
        "yse.send('ysemod.list', [1, 2.0, 3])\n");

    REQUIRE(pumpCount(iCap, 1));
    REQUIRE(pumpCount(fCap, 1));
    REQUIRE(pumpCount(sCap, 1));
    REQUIRE(pumpCount(lCap, 1));

    CHECK(std::get<int>(iCap.last()) == 5);
    CHECK(std::get<float>(fCap.last()) == doctest::Approx(0.5f));
    CHECK(std::get<std::string>(sCap.last()) == "hi");

    auto vec = std::get<std::vector<float>>(lCap.last());
    REQUIRE(vec.size() == 3);
    CHECK(vec[0] == doctest::Approx(1.0f));
    CHECK(vec[1] == doctest::Approx(2.0f));
    CHECK(vec[2] == doctest::Approx(3.0f));
  }

  TEST_CASE("yse module: on callback fires when the bus is published from C++") {
    if (!TestHelpers::engineInit()) return;

    BusCapture ready, echo;
    ScopedSub readySub("ysemod.on.ready", ready);
    ScopedSub echoSub("ysemod.on.echo", echo);

    // Register the subscriber, then a marker so we know the eval reached past it.
    run("yse.on('ysemod.on.in', lambda v: yse.send('ysemod.on.echo', v))\n"
        "yse.send('ysemod.on.ready', 1)\n");
    REQUIRE(pumpCount(ready, 1));

    // Publish from C++ (main thread). Delivery to the Python handler is deferred
    // to the next script-thread wake, then it echoes back.
    bus().publish("ysemod.on.in", BusValue{42}, YSE::T_GUI);
    REQUIRE(pumpCount(echo, 1));
    CHECK(std::get<int>(echo.last()) == 42);
  }

  TEST_CASE("yse module: latch caches the most recent value, None until first publish") {
    if (!TestHelpers::engineInit()) return;

    BusCapture before, after;
    ScopedSub beforeSub("ysemod.latch.before", before);
    ScopedSub afterSub("ysemod.latch.after", after);

    // l is a __main__ global so it survives to the second evaluation.
    run("l = yse.latch('ysemod.latch.x')\n"
        "yse.send('ysemod.latch.before', 1 if l.value is None else 0)\n");
    REQUIRE(pumpCount(before, 1));
    CHECK(std::get<int>(before.last()) == 1); // None before any publish

    bus().publish("ysemod.latch.x", BusValue{7.5f}, YSE::T_GUI);
    // Let the latch's subscription deliver the cached value on a script wake.
    for (int i = 0; i < 5; ++i) {
      YSE::System().update();
      YSE::System().sleep(10);
    }

    run("yse.send('ysemod.latch.after', l.value)\n");
    REQUIRE(pumpCount(after, 1));
    CHECK(std::get<float>(after.last()) == doctest::Approx(7.5f));
  }

  TEST_CASE("yse module: tick increments by 1 per system::update()") {
    if (!TestHelpers::engineInit()) return;

    BusCapture diff, len;
    ScopedSub diffSub("ysemod.tick.diff", diff);
    ScopedSub lenSub("ysemod.tick.len", len);

    // rec() records the current tick each tick and re-arms for the next one,
    // stopping after a bounded number of samples so the chain does not outlive
    // the test in the shared process.
    run("tlog = []\n"
        "def rec():\n"
        "    tlog.append(yse.tick)\n"
        "    if len(tlog) < 20:\n"
        "        yse.schedule(1, rec)\n"
        "rec()\n");

    // Pump several ticks so the chain accumulates samples.
    for (int i = 0; i < 12; ++i) {
      YSE::System().update();
      YSE::System().sleep(10);
    }

    run("yse.send('ysemod.tick.len', len(tlog))\n"
        "yse.send('ysemod.tick.diff', tlog[-1] - tlog[-2])\n");
    REQUIRE(pumpCount(len, 1));
    REQUIRE(pumpCount(diff, 1));

    CHECK(std::get<int>(len.last()) >= 3); // chain actually advanced
    CHECK(std::get<int>(diff.last()) == 1); // consecutive ticks differ by 1
  }

  TEST_CASE("yse module: schedule fires exactly N ticks later") {
    if (!TestHelpers::engineInit()) return;

    BusCapture start, fired;
    ScopedSub startSub("ysemod.sched.start", start);
    ScopedSub firedSub("ysemod.sched.fired", fired);

    run("sfire = [None]\n"
        "sstart = yse.tick\n"
        "def once():\n"
        "    sfire[0] = yse.tick\n"
        "yse.schedule(3, once)\n"
        "yse.send('ysemod.sched.start', sstart)\n");
    REQUIRE(pumpCount(start, 1));

    // Pump well past the 3-tick horizon, then read back the firing tick.
    for (int i = 0; i < 8; ++i) {
      YSE::System().update();
      YSE::System().sleep(10);
    }
    run("yse.send('ysemod.sched.fired', sfire[0] if sfire[0] is not None else -1)\n");
    REQUIRE(pumpCount(fired, 1));

    const int delta = std::get<int>(fired.last()) - std::get<int>(start.last());
    CHECK(delta == 3);
  }

  TEST_CASE("yse module: cancel_all deregisters older-generation subscriptions") {
    if (!TestHelpers::engineInit()) return;

    BusCapture ready, echo, afterCancel;
    ScopedSub readySub("ysemod.cancel.ready", ready);
    ScopedSub echoSub("ysemod.cancel.echo", echo);
    ScopedSub afterSub("ysemod.cancel.after", afterCancel);

    // Generation G: install a subscriber.
    run("yse.on('ysemod.cancel.in', lambda v: yse.send('ysemod.cancel.echo', v))\n"
        "yse.send('ysemod.cancel.ready', 1)\n");
    REQUIRE(pumpCount(ready, 1));

    // Generation G+1: cancel everything older — drops the subscriber above.
    run("yse.cancel_all()\n"
        "yse.send('ysemod.cancel.after', 1)\n");
    REQUIRE(pumpCount(afterCancel, 1));

    // Publishing now reaches no subscriber; the echo count must stay at zero.
    bus().publish("ysemod.cancel.in", BusValue{99}, YSE::T_GUI);
    for (int i = 0; i < 10; ++i) {
      YSE::System().update();
      YSE::System().sleep(10);
    }
    CHECK(echo.count() == 0);
  }

  TEST_CASE("yse module: cancel_all keeps same-generation registrations") {
    if (!TestHelpers::engineInit()) return;

    BusCapture ready, echo;
    ScopedSub readySub("ysemod.keep.ready", ready);
    ScopedSub echoSub("ysemod.keep.echo", echo);

    // cancel_all then subscribe in the SAME evaluation: the new sub survives.
    run("yse.cancel_all()\n"
        "yse.on('ysemod.keep.in', lambda v: yse.send('ysemod.keep.echo', v))\n"
        "yse.send('ysemod.keep.ready', 1)\n");
    REQUIRE(pumpCount(ready, 1));

    bus().publish("ysemod.keep.in", BusValue{3}, YSE::T_GUI);
    REQUIRE(pumpCount(echo, 1));
    CHECK(std::get<int>(echo.last()) == 3);
  }

  TEST_CASE("yse module: a schedule from an older generation still fires after a "
            "reload without cancel_all") {
    if (!TestHelpers::engineInit()) return;

    BusCapture ready, fired;
    ScopedSub readySub("ysemod.schedreload.ready", ready);
    ScopedSub firedSub("ysemod.schedreload.fired", fired);

    // Evaluation A (generation G): arm a schedule far enough out that it cannot
    // mature before evaluation B runs.
    run("yse.schedule(8, lambda: yse.send('ysemod.schedreload.fired', 1))\n");
    // Evaluation B (generation G+1): a plain marker, NO cancel_all.
    run("yse.send('ysemod.schedreload.ready', 1)\n");
    REQUIRE(pumpCount(ready, 1));

    // Pump past the 8-tick horizon: the gen-G schedule survived the reload.
    REQUIRE(pumpCount(fired, 1));
    CHECK(std::get<int>(fired.last()) == 1);
  }

  TEST_CASE("yse module: cancel_all on reload tears down an older-generation "
            "schedule before it fires") {
    if (!TestHelpers::engineInit()) return;

    BusCapture ready, fired;
    ScopedSub readySub("ysemod.schedcancel.ready", ready);
    ScopedSub firedSub("ysemod.schedcancel.fired", fired);

    // Evaluation A (generation G): same schedule as above.
    run("yse.schedule(8, lambda: yse.send('ysemod.schedcancel.fired', 1))\n");
    // Evaluation B (generation G+1): the editor prefix wipes generation G.
    run("yse.cancel_all()\n"
        "yse.send('ysemod.schedcancel.ready', 1)\n");
    REQUIRE(pumpCount(ready, 1));

    // Pump well past the horizon; the gen-G schedule must never fire.
    for (int i = 0; i < 20; ++i) {
      YSE::System().update();
      YSE::System().sleep(10);
    }
    CHECK(fired.count() == 0);
  }

  TEST_CASE("yse module: fresh_scope wipes earlier generations but keeps its own "
            "block's registrations") {
    if (!TestHelpers::engineInit()) return;

    BusCapture ready, oldEcho, newEcho;
    ScopedSub readySub("ysemod.fresh.ready", ready);
    ScopedSub oldEchoSub("ysemod.fresh.old.echo", oldEcho);
    ScopedSub newEchoSub("ysemod.fresh.new.echo", newEcho);

    // Evaluation A (generation G): an older-generation subscriber.
    run("yse.on('ysemod.fresh.old.in', "
        "lambda v: yse.send('ysemod.fresh.old.echo', v))\n");

    // Evaluation B (generation G+1): fresh_scope() cancels generation G on entry,
    // then registers a new subscriber inside the block — which must survive.
    run("with yse.fresh_scope():\n"
        "    yse.on('ysemod.fresh.new.in', "
        "lambda v: yse.send('ysemod.fresh.new.echo', v))\n"
        "yse.send('ysemod.fresh.ready', 1)\n");
    REQUIRE(pumpCount(ready, 1));

    // The new (in-block) subscriber fires; the old one was torn down.
    bus().publish("ysemod.fresh.new.in", BusValue{7}, YSE::T_GUI);
    REQUIRE(pumpCount(newEcho, 1));
    CHECK(std::get<int>(newEcho.last()) == 7);

    bus().publish("ysemod.fresh.old.in", BusValue{9}, YSE::T_GUI);
    for (int i = 0; i < 10; ++i) {
      YSE::System().update();
      YSE::System().sleep(10);
    }
    CHECK(oldEcho.count() == 0);
  }

  TEST_CASE("yse module: send rejects non-bus types via the error callback") {
    if (!TestHelpers::engineInit()) return;
    ErrSink sink;
    yse_set_script_error_callback(&captureErr, &sink);

    // bool is rejected (not coerced to int).
    run("yse.send('ysemod.err.bool', True)\n");
    REQUIRE(pumpUntil([&] { return sink.count >= 1; }));
    CHECK(sink.last.find("yse.send: value must be int, float, str, or list of numbers") !=
          std::string::npos);

    // dict is not a bus type either.
    run("yse.send('ysemod.err.dict', {})\n");
    REQUIRE(pumpUntil([&] { return sink.count >= 2; }));
    CHECK(sink.last.find("TypeError") != std::string::npos);

    // empty name is a ValueError.
    run("yse.send('', 1)\n");
    REQUIRE(pumpUntil([&] { return sink.count >= 3; }));
    CHECK(sink.last.find("ValueError") != std::string::npos);

    yse_set_script_error_callback(nullptr, nullptr);
  }

  TEST_CASE("yse module: an exception in an on callback routes to the error callback") {
    if (!TestHelpers::engineInit()) return;
    ErrSink sink;
    yse_set_script_error_callback(&captureErr, &sink);

    BusCapture ready;
    ScopedSub readySub("ysemod.cberr.ready", ready);

    run("yse.on('ysemod.cberr.in', lambda v: 1 / 0)\n"
        "yse.send('ysemod.cberr.ready', 1)\n");
    REQUIRE(pumpCount(ready, 1));

    bus().publish("ysemod.cberr.in", BusValue{1}, YSE::T_GUI);
    REQUIRE(pumpUntil([&] { return sink.count >= 1; }));
    CHECK(sink.last.find("ZeroDivisionError") != std::string::npos);

    yse_set_script_error_callback(nullptr, nullptr);
  }

} // TEST_SUITE("python")

#endif // YSE_ENABLE_PYTHON
