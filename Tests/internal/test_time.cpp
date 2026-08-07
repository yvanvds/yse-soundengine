// Tests for YSE::INTERNAL::time — the engine's elapsed-time source (issue #667).
//
// Every consumer of delta() reads it as *elapsed seconds*: the velocity
// derivations behind doppler (SOUND::implementationObject::update() and
// INTERNAL::listenerImplementation::update()), SOUND::Manager's file-GC
// throttle, and MUSIC::note's length countdown. The clock used to be
// std::clock(), which delivers neither of the two properties that reading
// implies:
//
//   * it measures *processor* time, not wall time. On glibc that is
//     CLOCK_PROCESS_CPUTIME_ID — the sum over every engine thread — so it ran
//     several times too fast while the engine was busy and stood almost still
//     while it idled.
//   * it is quantised to CLOCKS_PER_SEC, 1 ms on the MSVC / MSYS2 runtimes,
//     which is coarser than the audio callback period it is asked to measure.
//     Ticks shorter than a millisecond measured exactly 0 — the zero-length
//     tick that NaN'd the doppler ratio in issue #660.
//
// The cases below pin all three properties (wall time, thread-count immunity,
// sub-millisecond resolution) plus the seeding rule the switch to
// steady_clock introduced: steady_clock's epoch is unspecified, so the first
// update() must seed rather than report the time since system boot.
//
// Fail-without-fix: case 2 fails on every platform against std::clock()
// (millisecond quantisation makes most 200 us ticks measure 0); cases 3 and 4
// fail on POSIX, where std::clock() is processor time. All of them are driven
// through a local `time` instance rather than the INTERNAL::Time() singleton,
// so no other suite sharing this process is disturbed.

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>
#include "internal/time.h"

namespace {

  // Burn wall time on this thread without sleeping, so the interval is visible
  // to a wall clock *and* to a processor clock. The now() call in the condition
  // keeps the loop from being optimised away.
  void busySpin(std::chrono::microseconds duration) {
    const auto until = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < until) {
      // spin
    }
  }

} // namespace

TEST_SUITE("internal") {

  // ─── 1. seeding ─────────────────────────────────────────────────────────────

  TEST_CASE("internal time: the first tick seeds the clock instead of measuring the epoch (#667)") {
    YSE::INTERNAL::time t;
    CHECK(t.delta() == 0.f); // before any update

    t.update(); // seeding tick: nothing to measure against yet
    CHECK(t.delta() == 0.f);

    // Only now does the clock measure anything — and it measures the interval
    // since the seeding tick, not since steady_clock's (unspecified, in
    // practice boot-time) epoch.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    t.update();
    CHECK(t.delta() > 0.f);
    CHECK(t.delta() < 1.f);
  }

  // ─── 2. resolution ──────────────────────────────────────────────────────────

  TEST_CASE("internal time: sub-millisecond ticks are measured, not quantised away (#667)") {
    YSE::INTERNAL::time t;
    t.update(); // seed

    // 20 consecutive ticks of ~200 us. On the millisecond-quantised
    // std::clock() this replaced, a 200 us tick only measured non-zero when it
    // happened to straddle a clock tick — roughly one time in five — so
    // requiring all 20 to be non-zero fails against the old clock with
    // overwhelming probability. On a monotonic clock every one of them is a
    // real measurement.
    int zeroTicks = 0;
    for (int i = 0; i < 20; i++) {
      busySpin(std::chrono::microseconds(200));
      t.update();
      const Flt d = t.delta();
      CHECK(std::isfinite(d));
      if (d <= 0.f) zeroTicks++;
      CHECK(d < 0.05f); // a 200 us tick must not read as tens of milliseconds
    }
    CHECK(zeroTicks == 0);
  }

  // ─── 3. wall time, not processor time ───────────────────────────────────────

  TEST_CASE("internal time: an idle tick measures wall time, not consumed CPU (#667)") {
    YSE::INTERNAL::time t;
    t.update(); // seed

    // The thread sleeps: it burns essentially no processor time, so a CPU clock
    // reports ~0 for this interval while a wall clock reports 50 ms. Every
    // delta() consumer wants the latter.
    const auto before = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    t.update();
    const Flt wall = std::chrono::duration_cast<std::chrono::duration<Flt>>(
                         std::chrono::steady_clock::now() - before)
                         .count();

    CHECK(t.delta() > 0.04f); // pre-fix on POSIX: ~0
    CHECK(t.delta() < wall * 1.5f); // and not wildly over the real interval
  }

  // ─── 4. immunity to the engine's thread count ───────────────────────────────

  TEST_CASE("internal time: busy worker threads do not inflate the delta (#667)") {
    YSE::INTERNAL::time t;
    t.update(); // seed

    // Four threads burning CPU for the same wall interval. A process-CPU clock
    // sums all of them and reports ~5x the elapsed time — which, fed to the
    // velocity divides, understates every velocity by the same factor exactly
    // when the engine is under load.
    std::atomic<bool> stop{false};
    std::vector<std::thread> burners;
    burners.reserve(4);
    for (int i = 0; i < 4; i++) {
      burners.emplace_back([&stop] {
        while (!stop.load(std::memory_order_relaxed)) {
          busySpin(std::chrono::microseconds(500));
        }
      });
    }

    const auto before = std::chrono::steady_clock::now();
    busySpin(std::chrono::microseconds(40000));
    t.update();
    const Flt wall = std::chrono::duration_cast<std::chrono::duration<Flt>>(
                         std::chrono::steady_clock::now() - before)
                         .count();

    stop.store(true, std::memory_order_relaxed);
    for (auto& th : burners)
      th.join();

    CHECK(t.delta() > wall * 0.5f);
    CHECK(t.delta() < wall * 2.0f); // pre-fix on POSIX: ~5x wall, with 4 burners
  }

  // ─── 5. monotonicity ────────────────────────────────────────────────────────

  TEST_CASE("internal time: no tick ever measures a negative length (#667)") {
    YSE::INTERNAL::time t;
    t.update();
    for (int i = 0; i < 200; i++) {
      t.update(); // deliberately back to back: the shortest tick possible
      CHECK(t.delta() >= 0.f);
      CHECK(std::isfinite(t.delta()));
    }
  }

} // TEST_SUITE("internal")
