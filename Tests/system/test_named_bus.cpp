// Tests for INTERNAL::NamedBus (issue #121).
//
// Covers:
//   - subscribe → publish(T_GUI) → callback fires synchronously
//   - subscribe → publish(T_DSP) → callback fires after drainPending
//   - unsubscribe stops delivery
//   - 1000 subscribers + 1000 audio-path publishes: no heap allocations
//   - publish() to a name with no subscribers is a no-op (no allocation)
//
// The heap-allocation probe used by the no-allocation checks lives in the
// shared support/alloc_probe.{hpp,cpp} (issue #194 lifted it out of this TU so
// the manager / virtualFinder RT tests can reuse it).

#include <array>
#include <string>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "yse.hpp"
#include "internal/namedBus.h"
#include "support/alloc_probe.hpp"
#include "support/null_device.hpp"

namespace {
  using TestHelpers::g_alloc_count;
  using TestHelpers::ProbeScope;
} // namespace

TEST_SUITE("system") {

  TEST_CASE("named bus: subscribe then publish on T_GUI fires the callback synchronously") {
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    int received = 0;
    auto h = bus.subscribe("ch.test.gui", [&](const YSE::INTERNAL::BusValue& v) {
      if (auto* i = std::get_if<int>(&v)) received = *i;
    });

    bus.publish("ch.test.gui", YSE::INTERNAL::BusValue{42}, YSE::T_GUI);
    CHECK(received == 42);

    bus.unsubscribe(h);
  }

  TEST_CASE("named bus: T_DSP publish is queued and dispatched on drainPending") {
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    float received = 0.0f;
    int hits = 0;
    auto h = bus.subscribe("ch.test.dsp", [&](const YSE::INTERNAL::BusValue& v) {
      if (auto* f = std::get_if<float>(&v)) {
        received = *f;
        ++hits;
      }
    });

    bus.publish("ch.test.dsp", YSE::INTERNAL::BusValue{1.25f}, YSE::T_DSP);
    // Not delivered synchronously — still 0.
    CHECK(hits == 0);

    bus.drainPending();
    CHECK(hits == 1);
    CHECK(received == doctest::Approx(1.25f));

    bus.unsubscribe(h);
  }

  TEST_CASE("named bus: unsubscribe stops delivery on both paths") {
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    int count = 0;
    auto h = bus.subscribe("ch.test.unsub", [&](const YSE::INTERNAL::BusValue&) { ++count; });

    bus.publish("ch.test.unsub", YSE::INTERNAL::BusValue{1}, YSE::T_GUI);
    CHECK(count == 1);

    bus.unsubscribe(h);

    bus.publish("ch.test.unsub", YSE::INTERNAL::BusValue{2}, YSE::T_GUI);
    bus.publish("ch.test.unsub", YSE::INTERNAL::BusValue{3}, YSE::T_DSP);
    bus.drainPending();
    CHECK(count == 1);
  }

  TEST_CASE("named bus: publish to a name with no subscribers does not allocate") {
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    // Pre-construct the name so the probe scope only measures publish().
    const std::string name = "ch.test.empty";

    {
      ProbeScope probe;
      bus.publish(name, YSE::INTERNAL::BusValue{7}, YSE::T_GUI);
      bus.publish(name, YSE::INTERNAL::BusValue{0.5f}, YSE::T_DSP);
    }

    CHECK(g_alloc_count.load() == 0);
  }

  TEST_CASE("named bus: audio-thread publish path does not allocate at scale") {
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    constexpr int kSubs = 1000;
    constexpr int kPublishes = 1000;

    // Register 1000 subscribers spread across a few names. Pre-allocate the
    // handles vector outside the probe so the probe only sees publish().
    std::vector<YSE::INTERNAL::SubHandle> handles;
    handles.reserve(kSubs);

    int hits = 0;
    auto cb = [&](const YSE::INTERNAL::BusValue&) { ++hits; };

    const std::string nameA = "ch.scale.a";
    const std::string nameB = "ch.scale.b";

    for (int i = 0; i < kSubs; ++i) {
      handles.push_back(bus.subscribe((i & 1) ? nameA : nameB, cb));
    }

    YSE::INTERNAL::BusValue intVal{99};
    YSE::INTERNAL::BusValue fltVal{0.25f};

    {
      ProbeScope probe;
      for (int i = 0; i < kPublishes; ++i) {
        bus.publish((i & 1) ? nameA : nameB, (i & 1) ? intVal : fltVal, YSE::T_DSP);
      }
    }

    CHECK(g_alloc_count.load() == 0);

    bus.drainPending();
    // Sanity check that the messages actually made it through. Half of the
    // 1000 publishes go to nameA (500 subscribers each), the other half to
    // nameB (500 subscribers each) — so 1000 * 500 == 500'000 dispatches.
    CHECK(hits == kPublishes * (kSubs / 2));

    for (auto h : handles)
      bus.unsubscribe(h);
  }

  TEST_CASE("named bus: T_DSP publish drops string/list payloads silently") {
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    int hits = 0;
    auto h = bus.subscribe("ch.test.dsp.drop", [&](const YSE::INTERNAL::BusValue&) { ++hits; });

    // String and list payloads on T_DSP exceed the pooled-message footprint
    // and must be dropped silently rather than queued.
    bus.publish("ch.test.dsp.drop", YSE::INTERNAL::BusValue{std::string("nope")}, YSE::T_DSP);
    bus.publish("ch.test.dsp.drop", YSE::INTERNAL::BusValue{std::vector<float>{1.0f, 2.0f}},
                YSE::T_DSP);
    bus.drainPending();
    CHECK(hits == 0);

    // Sanity: the same payload still delivers on T_GUI.
    bus.publish("ch.test.dsp.drop", YSE::INTERNAL::BusValue{std::string("ok")}, YSE::T_GUI);
    CHECK(hits == 1);

    bus.unsubscribe(h);
  }

  TEST_CASE("named bus: concurrent T_DSP publishers each keep their own queue") {
    // Regression for issue #187: gSend publishes on T_DSP from whichever thread
    // renders the owning channel, and child channels render concurrently on
    // fast-pool workers. A single shared SPSC queue would have two producers
    // racing in inner_enqueue → torn indices and lost messages. Each producing
    // thread must own its own queue, so every message survives.
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    constexpr int kThreads = 4;
    constexpr int kPerThread = 500; // < per-queue capacity, so nothing drops

    std::array<int, kThreads> hits{};
    std::vector<YSE::INTERNAL::SubHandle> handles;
    handles.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      handles.push_back(
          bus.subscribe("ch.mt." + std::to_string(t), [t, &hits](const YSE::INTERNAL::BusValue& v) {
            if (std::get_if<int>(&v)) ++hits[t];
          }));
    }

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      workers.emplace_back([t, &bus] {
        const std::string name = "ch.mt." + std::to_string(t);
        for (int i = 0; i < kPerThread; ++i)
          bus.publish(name, YSE::INTERNAL::BusValue{i}, YSE::T_DSP);
      });
    }
    for (auto& w : workers)
      w.join();

    // Nothing is delivered until the consumer drains — on the main thread.
    bus.drainPending();

    for (int t = 0; t < kThreads; ++t)
      CHECK(hits[t] == kPerThread);

    for (auto h : handles)
      bus.unsubscribe(h);
  }

  TEST_CASE("named bus: off-control-thread T_GUI publish is deferred to drainPending") {
    // Regression for issue #193: a T_GUI publish is only safe to dispatch
    // inline on the control thread, because a subscriber callback runs an
    // interface setter that pushes into a per-implementation single-producer
    // message queue. The gMetro timer thread (and script threads) publish
    // T_GUI too; inline dispatch there would race the control thread on those
    // SPSC queues. Such publishes must be deferred to the control thread's
    // drainPending() instead. Before the fix this callback fired synchronously
    // on the worker thread (hits == 1 before the drain, dispatched on the
    // worker's id) — both checks below would fail.
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    std::atomic<int> hits{0};
    std::thread::id dispatchThread;
    auto h = bus.subscribe("ch.mt.gui", [&](const YSE::INTERNAL::BusValue& v) {
      if (std::get_if<int>(&v)) {
        dispatchThread = std::this_thread::get_id();
        hits.fetch_add(1, std::memory_order_relaxed);
      }
    });

    // Publish from a worker thread standing in for the gMetro timer thread.
    std::thread worker([&] { bus.publish("ch.mt.gui", YSE::INTERNAL::BusValue{7}, YSE::T_GUI); });
    worker.join();

    // Deferred, exactly like a T_DSP publish — nothing delivered yet.
    CHECK(hits.load() == 0);

    // Drained and dispatched on the control (test main) thread.
    bus.drainPending();
    CHECK(hits.load() == 1);
    CHECK(dispatchThread == std::this_thread::get_id());

    bus.unsubscribe(h);
  }

  TEST_CASE("named bus: control-thread T_GUI publish still dispatches synchronously") {
    // The fix must not regress the common case: a T_GUI publish issued on the
    // control thread (which is where the bus was constructed and where
    // drainPending() runs) still fires its subscribers inline, no drain needed.
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    int received = 0;
    auto h = bus.subscribe("ch.ct.gui", [&](const YSE::INTERNAL::BusValue& v) {
      if (auto* i = std::get_if<int>(&v)) received = *i;
    });

    bus.publish("ch.ct.gui", YSE::INTERNAL::BusValue{55}, YSE::T_GUI);
    CHECK(received == 55); // synchronous, before any drainPending()

    bus.unsubscribe(h);
  }

  TEST_CASE("named bus: deferred publish preserves string/list payloads") {
    // The off-control-thread inbox carries a full BusValue, so payload kinds
    // the fixed-footprint audio pool drops (string, list) survive the deferral
    // and arrive intact on drainPending() (issue #193).
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    std::string gotStr;
    std::vector<float> gotList;
    auto h = bus.subscribe("ch.mt.payload", [&](const YSE::INTERNAL::BusValue& v) {
      if (auto* s = std::get_if<std::string>(&v))
        gotStr = *s;
      else if (auto* l = std::get_if<std::vector<float>>(&v))
        gotList = *l;
    });

    std::thread worker([&] {
      bus.publish("ch.mt.payload", YSE::INTERNAL::BusValue{std::string("hi")}, YSE::T_GUI);
      bus.publish("ch.mt.payload", YSE::INTERNAL::BusValue{std::vector<float>{4.0f, 5.0f}},
                  YSE::T_GUI);
    });
    worker.join();

    CHECK(gotStr.empty());
    CHECK(gotList.empty());

    bus.drainPending();
    CHECK(gotStr == "hi");
    REQUIRE(gotList.size() == 2);
    CHECK(gotList[0] == doctest::Approx(4.0f));
    CHECK(gotList[1] == doctest::Approx(5.0f));

    bus.unsubscribe(h);
  }

  TEST_CASE("named bus: prefix tap receives every matching publish with its full name") {
    // Host prefix taps (issue #389): a tap registered on "tap.a." must see
    // every publish whose name starts with that prefix — including names that
    // have no exact-match subscriber at all (dispatch used to early-return on
    // an unknown name; taps must still be scanned).
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    std::vector<std::string> names;
    int ints = 0;
    auto t =
        bus.subscribeTap("tap.a.", [&](const std::string& name, const YSE::INTERNAL::BusValue& v) {
          names.push_back(name);
          if (std::get_if<int>(&v)) ++ints;
        });

    bus.publish("tap.a.play", YSE::INTERNAL::BusValue{1}, YSE::T_GUI);
    bus.publish("tap.a.stop", YSE::INTERNAL::BusValue{2}, YSE::T_GUI);
    bus.publish("tap.b.play", YSE::INTERNAL::BusValue{3}, YSE::T_GUI); // no match
    bus.publish("tap.a", YSE::INTERNAL::BusValue{4}, YSE::T_GUI); // shorter than prefix

    REQUIRE(names.size() == 2);
    CHECK(names[0] == "tap.a.play");
    CHECK(names[1] == "tap.a.stop");
    CHECK(ints == 2);

    bus.unsubscribeTap(t);
  }

  TEST_CASE("named bus: prefix tap delivers all five payload kinds incl. bang") {
    // The bus carries bang (monostate) besides int/float/string/list — gSend's
    // bang outlet publishes it. A tap must deliver every kind.
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    int bangs = 0, is = 0, fs = 0, ss = 0, ls = 0;
    auto t =
        bus.subscribeTap("tap.kinds.", [&](const std::string&, const YSE::INTERNAL::BusValue& v) {
          if (std::holds_alternative<std::monostate>(v))
            ++bangs;
          else if (std::holds_alternative<int>(v))
            ++is;
          else if (std::holds_alternative<float>(v))
            ++fs;
          else if (std::holds_alternative<std::string>(v))
            ++ss;
          else if (std::holds_alternative<std::vector<float>>(v))
            ++ls;
        });

    bus.publish("tap.kinds.bang", YSE::INTERNAL::BusValue{}, YSE::T_GUI);
    bus.publish("tap.kinds.int", YSE::INTERNAL::BusValue{7}, YSE::T_GUI);
    bus.publish("tap.kinds.float", YSE::INTERNAL::BusValue{0.5f}, YSE::T_GUI);
    bus.publish("tap.kinds.str", YSE::INTERNAL::BusValue{std::string("s")}, YSE::T_GUI);
    bus.publish("tap.kinds.list", YSE::INTERNAL::BusValue{std::vector<float>{1.0f}}, YSE::T_GUI);

    CHECK(bangs == 1);
    CHECK(is == 1);
    CHECK(fs == 1);
    CHECK(ss == 1);
    CHECK(ls == 1);

    bus.unsubscribeTap(t);
  }

  TEST_CASE("named bus: taps coexist with exact subscribers and unsubscribe cleanly") {
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    int exact = 0, tapAll = 0, tapNarrow = 0;
    auto h = bus.subscribe("tap.co.x", [&](const YSE::INTERNAL::BusValue&) { ++exact; });
    auto tAll =
        bus.subscribeTap("", // empty prefix matches every address
                         [&](const std::string&, const YSE::INTERNAL::BusValue&) { ++tapAll; });
    auto tNarrow = bus.subscribeTap(
        "tap.co.", [&](const std::string&, const YSE::INTERNAL::BusValue&) { ++tapNarrow; });

    bus.publish("tap.co.x", YSE::INTERNAL::BusValue{1}, YSE::T_GUI);
    CHECK(exact == 1);
    CHECK(tapAll == 1);
    CHECK(tapNarrow == 1);

    bus.publish("elsewhere", YSE::INTERNAL::BusValue{2}, YSE::T_GUI);
    CHECK(tapAll == 2);
    CHECK(tapNarrow == 1);

    bus.unsubscribeTap(tNarrow);
    bus.publish("tap.co.x", YSE::INTERNAL::BusValue{3}, YSE::T_GUI);
    CHECK(exact == 2);
    CHECK(tapAll == 3);
    CHECK(tapNarrow == 1); // unsubscribed — no further delivery

    bus.unsubscribeTap(tAll);
    bus.unsubscribe(h);
  }

  TEST_CASE("named bus: T_DSP and deferred T_GUI publishes reach taps on drainPending") {
    // Audio-thread and off-control-thread publishes are already funnelled into
    // dispatch() via drainPending(); the tap rides that path with zero extra
    // audio-thread machinery.
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    int hits = 0;
    auto t = bus.subscribeTap("tap.drain.",
                              [&](const std::string&, const YSE::INTERNAL::BusValue&) { ++hits; });

    bus.publish("tap.drain.dsp", YSE::INTERNAL::BusValue{1}, YSE::T_DSP);
    std::thread worker(
        [&] { bus.publish("tap.drain.gui", YSE::INTERNAL::BusValue{2}, YSE::T_GUI); });
    worker.join();
    CHECK(hits == 0); // nothing delivered before the drain

    bus.drainPending();
    CHECK(hits == 2);

    bus.unsubscribeTap(t);
  }

  TEST_CASE("named bus: duplicate subscriptions on a name each get called") {
    REQUIRE(TestHelpers::engineInit());
    auto& bus = YSE::INTERNAL::Bus();

    int a = 0, b = 0;
    auto h1 = bus.subscribe("ch.test.dup", [&](const YSE::INTERNAL::BusValue&) { ++a; });
    auto h2 = bus.subscribe("ch.test.dup", [&](const YSE::INTERNAL::BusValue&) { ++b; });

    bus.publish("ch.test.dup", YSE::INTERNAL::BusValue{1}, YSE::T_GUI);
    CHECK(a == 1);
    CHECK(b == 1);

    bus.unsubscribe(h1);
    bus.unsubscribe(h2);
  }

} // TEST_SUITE("system")
