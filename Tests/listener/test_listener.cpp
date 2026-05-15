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
//     (sign, finiteness, zero-on-stationary; magnitude is not asserted because
//      std::clock() resolution makes the exact delta non-portable)
//   - listenerImplementation::getPos() — exposes the scaled newPos
//
// Engine init: the listener singleton lives independently of PortAudio, but
// other tests assume engineInit() ran (and ListenerImpl() is touched by sound
// updates).  We guard on engineInit() to stay consistent with the rest of the
// suite.

#include <doctest/doctest.h>
#include <chrono>
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
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    YSE::INTERNAL::Time().update();
    YSE::INTERNAL::ListenerImpl().update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    YSE::INTERNAL::Time().update();
    YSE::INTERNAL::ListenerImpl().update();
}

// Sleep long enough for std::clock() to advance (CLOCKS_PER_SEC granularity on
// Windows is 1ms; on Linux 10ms).  20ms is a safe floor for both.
inline void advanceClock() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    YSE::INTERNAL::Time().update();
}

} // namespace

TEST_SUITE("listener") {

// ─── Singleton identity ──────────────────────────────────────────────────────

TEST_CASE("listener: Listener() returns the same singleton on every call") {
    if (!TestHelpers::engineInit()) return;
    YSE::listener & a = YSE::Listener();
    YSE::listener & b = YSE::Listener();
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
    YSE::listener & ref = YSE::Listener().pos(YSE::Pos(0.f, 0.f, 0.f));
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
    YSE::listener & ref = YSE::Listener().orient(YSE::Pos(0.f, 0.f, 0.f), YSE::Pos(0.f, 1.f, 0.f));
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

TEST_CASE("listener impl: distanceFactor=2 doubles the effective velocity vs factor=1") {
    if (!TestHelpers::engineInit()) return;

    // Run the same one-unit step twice — once with distanceFactor=1, once with
    // distanceFactor=2 — and compare the resulting vel.x.  Time().delta() will
    // vary between the two runs, so use a loose ratio check: vel scales with
    // distanceFactor (newPos = pos * distanceFactor), so factor=2 should be
    // strictly greater for the same pos delta.
    resetListenerState();
    advanceClock();
    YSE::Listener().pos(YSE::Pos(1.f, 0.f, 0.f));
    YSE::INTERNAL::ListenerImpl().update();
    const float vWithFactor1 = YSE::Listener().vel().x;

    resetListenerState();
    YSE::INTERNAL::Settings().distanceFactor = 2.f;
    // After resetListenerState() the impl's lastPos is (0,0,0) scaled by the
    // CURRENT distanceFactor (now 2) — but pos was set to (0,0,0), so
    // lastPos = (0,0,0) * 2 = (0,0,0).  Safe.
    advanceClock();
    YSE::Listener().pos(YSE::Pos(1.f, 0.f, 0.f));
    YSE::INTERNAL::ListenerImpl().update();
    const float vWithFactor2 = YSE::Listener().vel().x;

    CHECK(vWithFactor1 > 0.f);
    CHECK(vWithFactor2 > vWithFactor1);

    YSE::INTERNAL::Settings().distanceFactor = 1.f;
}

TEST_CASE("listener impl: getPos() returns position scaled by distanceFactor") {
    if (!TestHelpers::engineInit()) return;
    resetListenerState();
    YSE::INTERNAL::Settings().distanceFactor = 3.f;
    YSE::Listener().pos(YSE::Pos(1.f, 2.f, 4.f));
    YSE::INTERNAL::Time().update();
    YSE::INTERNAL::ListenerImpl().update();
    const YSE::Pos & p = YSE::INTERNAL::ListenerImpl().getPos();
    CHECK(p.x == doctest::Approx(3.f));
    CHECK(p.y == doctest::Approx(6.f));
    CHECK(p.z == doctest::Approx(12.f));
    YSE::INTERNAL::Settings().distanceFactor = 1.f;
}

} // TEST_SUITE("listener")
