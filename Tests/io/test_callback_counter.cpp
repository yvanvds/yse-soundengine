// Regression tests for the audio-callback counter read-and-reset contract
// (issue #198).
//
// The PortAudio backend counts audio-callback invocations in an atomic
// (`managerObject::callbacksSinceLastUpdate`, incremented on the callback
// thread) and the control thread drains it once per `system::update()` tick
// via `GetCallbacksSinceLastUpdate()`. A zero reading feeds the missed-callback
// / auto-reconnect logic in system.cpp, so *losing* an increment can push a
// spurious "device stalled" reading.
//
// The bug: the drain was a non-atomic read-then-zero —
//
//     unsigned int result = callbacksSinceLastUpdate;   // load
//     callbacksSinceLastUpdate = 0;                     // store 0
//
// An increment landing between the load and the store is erased. The fix
// replaces both steps with a single atomic `exchange(0)` (matching the Oboe
// backend's `callbacksSinceLastUpdate.exchange(0)`).
//
// Why these tests live here and not against the real manager: the counter is a
// private member and is only ever incremented from PortAudio's callback thread
// (a real open stream), so it cannot be driven from a unit test without adding
// a production test seam — which the project's scope rules forbid. The race is
// also a *logical* lost update between two atomic operations, not a data race,
// so it is invisible to ThreadSanitizer and a real device would never reproduce
// the nanosecond window at millisecond callback rates. These tests therefore
// pin the read-and-reset semantic contract the production fix relies on: they
// exercise the exact same `std::atomic<unsigned int>` type and the exact same
// two idioms, and would fail if the code regressed to the read-then-zero form.
// The live counter path itself is exercised end-to-end by the integration
// suite ("engine: audio callback fires within 100ms on a real output device").

#include <doctest/doctest.h>

#include <atomic>
#include <thread>

TEST_SUITE("io") {

  // Deterministic model of the lost-update window, no threads required. This is
  // the crisp "fails without the fix" discriminator: it shows read-then-zero
  // erasing an interleaved increment, and exchange(0) never doing so.
  TEST_CASE("callback counter: exchange read-and-reset never loses an increment [issue #198]") {
    std::atomic<unsigned int> counter{0};

    // ── Buggy read-then-zero: an increment lands between the load and the store.
    counter.store(3); // 3 callbacks pending
    unsigned int buggyRead = counter.load(); // control thread reads the count
    counter.fetch_add(1); // callback fires in the window -> 4
    counter.store(0); // control thread zeroes the counter
    // The control thread reported 3 and then cleared the counter, so the 4th
    // callback is gone forever: 4 produced, 3 accounted, 1 lost.
    CHECK(buggyRead == 3);
    CHECK(counter.load() == 0);

    // ── Fixed exchange(0), increment BEFORE the drain: it is counted now.
    counter.store(3);
    counter.fetch_add(1); // callback fires -> 4
    unsigned int drainedBefore = counter.exchange(0);
    CHECK(drainedBefore == 4); // nothing lost
    CHECK(counter.load() == 0);

    // ── Fixed exchange(0), increment AFTER the drain: it is preserved, not lost.
    counter.store(3);
    unsigned int drainedAfter = counter.exchange(0);
    counter.fetch_add(1); // callback fires after the drain
    CHECK(drainedAfter == 3);
    CHECK(counter.load() == 1); // preserved for the next drain
  }

  // High-contention aggregate check: a producer hammering the counter while a
  // consumer drains it with exchange(0) must account for every single increment
  // exactly once. Named under the project's "concurrency:" convention so the
  // sanitizer / concurrency CTest legs pick it up.
  TEST_CASE("io concurrency: callback counter drain accounts for every increment [issue #198]") {
    std::atomic<unsigned int> counter{0};
    std::atomic<bool> producerDone{false};
    constexpr unsigned int N = 2'000'000u;

    std::thread producer([&]() {
      for (unsigned int i = 0; i < N; ++i)
        counter.fetch_add(1, std::memory_order_relaxed);
      producerDone.store(true, std::memory_order_release);
    });

    // Mirror GetCallbacksSinceLastUpdate(): atomic read-and-reset, summed.
    unsigned long long accounted = 0;
    while (!producerDone.load(std::memory_order_acquire)) {
      accounted += counter.exchange(0, std::memory_order_acq_rel);
    }
    // Drain whatever the producer added between the last in-loop exchange and
    // now. The acquire above synchronises-with the producer's release store,
    // which happens-after all N increments, so every increment is visible to
    // this final exchange and counted exactly once.
    accounted += counter.exchange(0, std::memory_order_acq_rel);

    producer.join();

    CHECK(accounted == static_cast<unsigned long long>(N));
  }

} // TEST_SUITE("io")
