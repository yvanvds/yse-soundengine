// Tests for INTERNAL::NamedBus (issue #121).
//
// Covers:
//   - subscribe → publish(T_GUI) → callback fires synchronously
//   - subscribe → publish(T_DSP) → callback fires after drainPending
//   - unsubscribe stops delivery
//   - 1000 subscribers + 1000 audio-path publishes: no heap allocations
//   - publish() to a name with no subscribers is a no-op (no allocation)
//
// Allocation probe: the operator-new replacement below is a "replaceable
// allocation function" per [basic.stc.dynamic.allocation]. It counts calls
// only while `g_alloc_probe_active` is true so the rest of the test binary
// (doctest, STL, std::function, ...) is unaffected. The probe lives in this
// TU because no other test needs it; if a future TU also wants it, lift the
// probe into a shared header.

#include <array>
#include <atomic>
#include <cstdlib>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "yse.hpp"
#include "internal/namedBus.h"
#include "support/null_device.hpp"

namespace {

  std::atomic<int> g_alloc_count{0};
  std::atomic<bool> g_alloc_probe_active{false};

  struct ProbeScope {
    ProbeScope() {
      g_alloc_count = 0;
      g_alloc_probe_active = true;
    }
    ~ProbeScope() {
      g_alloc_probe_active = false;
    }
  };

} // namespace

// ThreadSanitizer ships its own replaceable operator new/delete in
// libclang_rt.tsan_cxx, so defining ours too is a multiple-definition link
// error (issue #229 wired a TSan build of this binary). Skip the allocation
// probe under TSan: the audio-path checks below assert g_alloc_count == 0, which
// then holds trivially because the counter is never touched. ASan tolerates the
// override, so it is kept there.
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define YSE_UNDER_TSAN 1
#endif
#endif
#if defined(__SANITIZE_THREAD__)
#define YSE_UNDER_TSAN 1
#endif

#ifndef YSE_UNDER_TSAN
void* operator new(std::size_t n) {
  if (g_alloc_probe_active.load(std::memory_order_relaxed))
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
  throw std::bad_alloc{};
}

// Route the nothrow form through malloc too. libsndfile's sndfile.hh allocates
// SNDFILE_ref with `new (std::nothrow)`; without this override that allocation
// would go through the default (ASan-instrumented) operator new while the
// matching delete below frees it with std::free, which AddressSanitizer flags
// as an alloc-dealloc-mismatch (issue #219).
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  if (g_alloc_probe_active.load(std::memory_order_relaxed))
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(n == 0 ? 1 : n);
}

void operator delete(void* p) noexcept {
  std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
  std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept {
  std::free(p);
}
#endif // YSE_UNDER_TSAN

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
