// Tests for YSE::listener (YseEngine/listener.cpp) and the underlying
// INTERNAL::listenerImplementation (YseEngine/implementations/listenerImplementation.cpp).
//
// Coverage:
//   - Listener() singleton identity
//   - pos() / vel() / forward() / upward() default state after a reset
//   - pos(Pos) setter writes through and returns *this
//   - orient(forward, up) sets both vectors; default up parameter is (0,1,0)
//   - orient() returns *this
//   - listenerImplementation::update() — newPos = pos * distanceFactor
//   - listenerImplementation::update() — vel = (newPos - lastPos) / Time().delta()
//     (sign, finiteness, zero-on-stationary, and — since INTERNAL::time moved to
//      a monotonic wall clock in #667 — the magnitude, bracketed against an
//      independent steady_clock measurement of the same tick)
//   - listenerImplementation::getPos() — exposes the scaled newPos
//
// Engine init: the listener singleton lives independently of PortAudio, but
// other tests assume engineInit() ran (and ListenerImpl() is touched by sound
// updates).  We guard on engineInit() to stay consistent with the rest of the
// suite.

#include <doctest/doctest.h>
#include <chrono>
#include <cmath>
#include <thread>
#include "listener.hpp"
#include "implementations/listenerImplementation.h"
#include "internal/settings.h"
#include "internal/time.h"
#include "support/null_device.hpp"

namespace {

  // Reset listener + distanceFactor to a known baseline so test order is
  // irrelevant.  Two sleep-bracketed updates fully settle the impl:
  //   1st: seeds lastPos = (0,0,0) but produces vel = (0 - prev)/delta
  //   2nd: with stationary pos, produces vel = (0 - 0)/delta = 0
  // The sleeps guarantee Time().delta() > 0 between updates — otherwise the
  // 1/delta term in listenerImpl::update() blows up to (0 * inf) = NaN and
  // contaminates vel for subsequent tests.
  inline void resetListenerState() {
    YSE::INTERNAL::Settings().distanceFactor = 1.f;
    YSE::Listener().pos(YSE::Pos(0.f, 0.f, 0.f));
    YSE::Listener().orient(YSE::Pos(0.f, 0.f, 0.f), YSE::Pos(0.f, 1.f, 0.f));
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    YSE::INTERNAL::Time().update();
    YSE::INTERNAL::ListenerImpl().update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    YSE::INTERNAL::Time().update();
    YSE::INTERNAL::ListenerImpl().update();
  }

  // Close one update tick with a measurable length. This used to sleep 20 ms
  // because std::clock() is quantised to CLOCKS_PER_SEC (1 ms on Windows,
  // coarser elsewhere) and a shorter tick could measure exactly 0; the
  // monotonic clock of issue #667 resolves well under a microsecond, so 1 ms is
  // now more than enough.
  inline void advanceClock() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    YSE::INTERNAL::Time().update();
  }

  // Burn wall time on this thread without sleeping, for tick lengths short
  // enough that sleep_for's own granularity would dominate them.
  inline void busySpin(std::chrono::microseconds duration) {
    const auto until = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < until) {
      // spin
    }
  }

  inline Flt secondsSince(std::chrono::steady_clock::time_point from,
                          std::chrono::steady_clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::duration<Flt>>(to - from).count();
  }

} // namespace

TEST_SUITE("listener") {

  // ─── Singleton identity ──────────────────────────────────────────────────────

  TEST_CASE("listener: Listener() returns the same singleton on every call") {
    if (!TestHelpers::engineInit()) return;
    YSE::listener& a = YSE::Listener();
    YSE::listener& b = YSE::Listener();
    CHECK(&a == &b);
  }

  // ─── Default state ────────────────────────────────────────────────────────────

  TEST_CASE("listener: position reads back as origin after reset") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    YSE::Pos p = YSE::Listener().pos();
    CHECK(p.x == doctest::Approx(0.f));
    CHECK(p.y == doctest::Approx(0.f));
    CHECK(p.z == doctest::Approx(0.f));
  }

  TEST_CASE("listener: forward reads back as zero vector after reset") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    YSE::Pos f = YSE::Listener().forward();
    CHECK(f.x == doctest::Approx(0.f));
    CHECK(f.y == doctest::Approx(0.f));
    CHECK(f.z == doctest::Approx(0.f));
  }

  TEST_CASE("listener: upward defaults to (0,1,0) after reset") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    YSE::Pos u = YSE::Listener().upward();
    CHECK(u.x == doctest::Approx(0.f));
    CHECK(u.y == doctest::Approx(1.f));
    CHECK(u.z == doctest::Approx(0.f));
  }

  // ─── pos() setter ────────────────────────────────────────────────────────────

  TEST_CASE("listener: pos(Pos) writes through to subsequent pos() read") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    YSE::Listener().pos(YSE::Pos(1.f, 2.f, 3.f));
    YSE::Pos p = YSE::Listener().pos();
    CHECK(p.x == doctest::Approx(1.f));
    CHECK(p.y == doctest::Approx(2.f));
    CHECK(p.z == doctest::Approx(3.f));
  }

  TEST_CASE("listener: pos(Pos) returns the same listener for chaining") {
    if (!TestHelpers::engineInit()) return;
    YSE::listener& ref = YSE::Listener().pos(YSE::Pos(0.f, 0.f, 0.f));
    CHECK(&ref == &YSE::Listener());
  }

  TEST_CASE("listener: negative coordinates round-trip through pos()") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    YSE::Listener().pos(YSE::Pos(-4.5f, -0.25f, -100.f));
    YSE::Pos p = YSE::Listener().pos();
    CHECK(p.x == doctest::Approx(-4.5f));
    CHECK(p.y == doctest::Approx(-0.25f));
    CHECK(p.z == doctest::Approx(-100.f));
  }

  // ─── orient() setter ──────────────────────────────────────────────────────────

  TEST_CASE("listener: orient(forward, up) writes both vectors through") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    YSE::Listener().orient(YSE::Pos(1.f, 0.f, 0.f), YSE::Pos(0.f, 0.f, 1.f));
    YSE::Pos f = YSE::Listener().forward();
    YSE::Pos u = YSE::Listener().upward();
    CHECK(f.x == doctest::Approx(1.f));
    CHECK(f.y == doctest::Approx(0.f));
    CHECK(f.z == doctest::Approx(0.f));
    CHECK(u.x == doctest::Approx(0.f));
    CHECK(u.y == doctest::Approx(0.f));
    CHECK(u.z == doctest::Approx(1.f));
  }

  TEST_CASE("listener: orient(forward) uses default up = (0,1,0)") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    // First disturb up so the default-arg overload has something to overwrite.
    YSE::Listener().orient(YSE::Pos(0.f, 0.f, 1.f), YSE::Pos(1.f, 0.f, 0.f));
    YSE::Listener().orient(YSE::Pos(1.f, 0.f, 0.f));
    YSE::Pos u = YSE::Listener().upward();
    CHECK(u.x == doctest::Approx(0.f));
    CHECK(u.y == doctest::Approx(1.f));
    CHECK(u.z == doctest::Approx(0.f));
  }

  TEST_CASE("listener: orient(forward, up) returns the same listener for chaining") {
    if (!TestHelpers::engineInit()) return;
    YSE::listener& ref = YSE::Listener().orient(YSE::Pos(0.f, 0.f, 0.f), YSE::Pos(0.f, 1.f, 0.f));
    CHECK(&ref == &YSE::Listener());
  }

  // ─── velocity / impl update ──────────────────────────────────────────────────

  TEST_CASE("listener: velocity is zero after reset") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    YSE::Pos v = YSE::Listener().vel();
    CHECK(v.x == doctest::Approx(0.f));
    CHECK(v.y == doctest::Approx(0.f));
    CHECK(v.z == doctest::Approx(0.f));
  }

  TEST_CASE("listener impl: positive X motion produces positive vel.x") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    advanceClock();
    YSE::Listener().pos(YSE::Pos(1.f, 0.f, 0.f));
    YSE::INTERNAL::ListenerImpl().update();
    YSE::Pos v = YSE::Listener().vel();
    CHECK(v.x > 0.f);
    CHECK(v.y == doctest::Approx(0.f));
    CHECK(v.z == doctest::Approx(0.f));
  }

  TEST_CASE("listener impl: negative X motion produces negative vel.x") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    advanceClock();
    YSE::Listener().pos(YSE::Pos(-1.f, 0.f, 0.f));
    YSE::INTERNAL::ListenerImpl().update();
    YSE::Pos v = YSE::Listener().vel();
    CHECK(v.x < 0.f);
  }

  TEST_CASE("listener impl: pure Y motion populates only vel.y") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    advanceClock();
    YSE::Listener().pos(YSE::Pos(0.f, 2.f, 0.f));
    YSE::INTERNAL::ListenerImpl().update();
    YSE::Pos v = YSE::Listener().vel();
    CHECK(v.x == doctest::Approx(0.f));
    CHECK(v.y > 0.f);
    CHECK(v.z == doctest::Approx(0.f));
  }

  TEST_CASE("listener impl: stationary position over two updates yields zero velocity") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    advanceClock();
    // No position change — newPos == lastPos.
    YSE::INTERNAL::ListenerImpl().update();
    YSE::Pos v = YSE::Listener().vel();
    CHECK(v.x == doctest::Approx(0.f));
    CHECK(v.y == doctest::Approx(0.f));
    CHECK(v.z == doctest::Approx(0.f));
  }

  TEST_CASE("listener impl: velocity magnitude matches distance over the real tick length (#667)") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();

    // The listener covers exactly one unit over one update tick, so
    // Listener().vel().x must read back as 1 / (tick length in seconds). The
    // tick is bracketed by this thread's own steady_clock: the engine's tick
    // starts inside [outerStart, innerStart] and ends inside [innerEnd,
    // outerEnd], so its length is in [inner, outer] no matter how the scheduler
    // interleaves — and the velocity is therefore in [1/outer, 1/inner]. That
    // holds through any preemption, which is what makes an *exact-magnitude*
    // assertion safe here at all.
    //
    // The tick is deliberately sub-millisecond: std::clock(), which INTERNAL::
    // time used before #667, is quantised to 1 ms on the MSVC / MSYS2 runtimes,
    // so it reported this tick as either 0 s (velocity held at the previous
    // measurement — 0 here, below the lower bound) or 1 ms (velocity ~1000,
    // less than half the true value). It is also the accuracy the doppler path
    // actually needs: an audio callback is shorter than a millisecond.
    const auto outerStart = std::chrono::steady_clock::now();
    YSE::INTERNAL::Time().update();
    const auto innerStart = std::chrono::steady_clock::now();

    busySpin(std::chrono::microseconds(400));
    YSE::Listener().pos(YSE::Pos(1.f, 0.f, 0.f));

    const auto innerEnd = std::chrono::steady_clock::now();
    YSE::INTERNAL::Time().update();
    const auto outerEnd = std::chrono::steady_clock::now();
    YSE::INTERNAL::ListenerImpl().update();

    const Flt outer = secondsSince(outerStart, outerEnd); // >= the engine's tick
    const Flt inner = secondsSince(innerStart, innerEnd); // <= the engine's tick
    REQUIRE(inner > 0.f);

    const YSE::Pos v = YSE::Listener().vel();
    INFO("tick bracketed to [" << inner << ", " << outer << "] s, vel.x = " << v.x);
    CHECK(std::isfinite(v.x));
    CHECK(v.x >= (1.f / outer) * 0.95f);
    CHECK(v.x <= (1.f / inner) * 1.05f);
    CHECK(v.y == doctest::Approx(0.f));
    CHECK(v.z == doctest::Approx(0.f));
  }

  TEST_CASE("listener impl: a zero-length tick holds the last velocity instead of NaN (#660)") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    advanceClock();
    YSE::Listener().pos(YSE::Pos(1.f, 0.f, 0.f));
    YSE::INTERNAL::ListenerImpl().update();
    const YSE::Pos measured = YSE::Listener().vel();
    REQUIRE(measured.x > 0.f);

    // Two Time().update() calls back to back, with no sleep between them: the
    // shortest tick this code can produce. On the millisecond-quantised
    // std::clock() that INTERNAL::time used before #667 it measured delta == 0
    // outright; on the monotonic clock it usually measures a real (tiny) tick,
    // so the case now asserts the safe outcome on either reading and the pure
    // zero-delta guard is pinned in Tests/dsp/test_panner.cpp. It stays here
    // because a clock with a coarse period — some embedded steady_clock
    // implementations, and Android under load — can still report 0.
    // Pre-fix, update() then divided by it — 1/0 == inf, and for a
    // stationary listener (newPos == lastPos) 0 * inf == NaN. That NaN is
    // published to every sound as listenerVelocity and walks through
    // computeDopplerRatio's comparisons into the playback rate (issue #660).
    YSE::INTERNAL::Time().update();
    YSE::INTERNAL::Time().update();
    const bool zeroTick = (YSE::INTERNAL::Time().delta() == 0.f);
    YSE::INTERNAL::ListenerImpl().update();
    const YSE::Pos v = YSE::Listener().vel();

    INFO("zero-length tick observed: " << zeroTick);
    CHECK(std::isfinite(v.x));
    CHECK(std::isfinite(v.y));
    CHECK(std::isfinite(v.z));
    if (zeroTick) {
      // Nothing was measured, so the previous measurement stands unchanged.
      CHECK(v.x == doctest::Approx(measured.x));
      CHECK(v.y == doctest::Approx(measured.y));
      CHECK(v.z == doctest::Approx(measured.z));
    } else {
      // The clock did advance, so this is an ordinary stationary tick.
      CHECK(v.x == doctest::Approx(0.f));
    }
  }

  TEST_CASE("listener impl: velocity is positive on a one-unit step under either distanceFactor") {
    if (!TestHelpers::engineInit()) return;

    // We previously asserted vWithFactor2 > vWithFactor1 across two independent
    // update() calls. Both velocities are 1/Time().delta() and 2/Time().delta()
    // in form, but Time().delta() varies between iterations (especially on
    // mobile/Android schedulers) and the cross-iteration comparison races the
    // clock — see issue #75. The "factor scales position" property is already
    // covered by "getPos() returns position scaled by distanceFactor" below.
    resetListenerState();
    advanceClock();
    YSE::Listener().pos(YSE::Pos(1.f, 0.f, 0.f));
    YSE::INTERNAL::ListenerImpl().update();
    CHECK(YSE::Listener().vel().x > 0.f);

    resetListenerState();
    YSE::INTERNAL::Settings().distanceFactor = 2.f;
    advanceClock();
    YSE::Listener().pos(YSE::Pos(1.f, 0.f, 0.f));
    YSE::INTERNAL::ListenerImpl().update();
    CHECK(YSE::Listener().vel().x > 0.f);

    YSE::INTERNAL::Settings().distanceFactor = 1.f;
  }

  TEST_CASE("listener impl: getPos() returns position scaled by distanceFactor") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    YSE::INTERNAL::Settings().distanceFactor = 3.f;
    YSE::Listener().pos(YSE::Pos(1.f, 2.f, 4.f));
    YSE::INTERNAL::Time().update();
    YSE::INTERNAL::ListenerImpl().update();
    const YSE::Pos& p = YSE::INTERNAL::ListenerImpl().getPos();
    CHECK(p.x == doctest::Approx(3.f));
    CHECK(p.y == doctest::Approx(6.f));
    CHECK(p.z == doctest::Approx(12.f));
    YSE::INTERNAL::Settings().distanceFactor = 1.f;
  }

} // TEST_SUITE("listener")
