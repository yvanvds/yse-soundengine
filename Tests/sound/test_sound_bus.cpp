// Tests for YSE::sound named-bus addressing (issue #123, epic #119).
//
// A named sound subscribes to "sound.<name>.volume", ".speed" and ".position"
// on the global named bus. Publishing on T_GUI dispatches synchronously, so
// these tests publish and then read the interface getter back without pumping
// an update tick. Engine-dependent cases guard on engineInit() (audio paused),
// matching the rest of the sound suite.

#include <doctest/doctest.h>

#include <string>
#include <thread>
#include <vector>

#include "sound/soundInterface.hpp"
#include "dsp/dspObject.hpp"
#include "internal/namedBus.h"
#include "support/null_device.hpp"

namespace {
  // Minimal no-op DSP source: lets us create a valid sound without file I/O or a
  // live audio device (same trick as test_sound_state.cpp).
  struct SilentSource : YSE::DSP::dspSourceObject {
    void process(YSE::SOUND_STATUS&) override {}
    void frequency(float) override {}
  };

  void publishFloat(const std::string& name, float v) {
    YSE::INTERNAL::Bus().publish(name, YSE::INTERNAL::BusValue{v}, YSE::T_GUI);
  }
} // namespace

TEST_SUITE("sound") {

  TEST_CASE("sound bus: volume publish reaches the sound") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.name("kick");

    publishFloat("sound.kick.volume", 0.5f);
    CHECK(s.volume() == doctest::Approx(0.5f));
  }

  TEST_CASE("sound bus: speed publish reaches the sound") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.name("speedy");

    publishFloat("sound.speedy.speed", 2.0f);
    CHECK(s.speed() == doctest::Approx(2.0f));
  }

  TEST_CASE("sound bus: position publish (list of 3) becomes a Pos") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.name("mover");

    YSE::INTERNAL::Bus().publish("sound.mover.position",
                                 YSE::INTERNAL::BusValue{std::vector<float>{1.0f, 2.0f, 3.0f}},
                                 YSE::T_GUI);

    YSE::Pos p = s.pos();
    CHECK(p.x == doctest::Approx(1.0f));
    CHECK(p.y == doctest::Approx(2.0f));
    CHECK(p.z == doctest::Approx(3.0f));
  }

  TEST_CASE("sound bus: position publish with wrong arity is ignored") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.name("arity");

    // Length != 3 must not move the sound (stays at the origin default).
    YSE::INTERNAL::Bus().publish("sound.arity.position",
                                 YSE::INTERNAL::BusValue{std::vector<float>{1.0f, 2.0f}},
                                 YSE::T_GUI);

    YSE::Pos p = s.pos();
    CHECK(p.x == doctest::Approx(0.0f));
    CHECK(p.y == doctest::Approx(0.0f));
    CHECK(p.z == doctest::Approx(0.0f));
  }

  TEST_CASE("sound bus: int publish is coerced to float for volume") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.name("coerce");

    YSE::INTERNAL::Bus().publish("sound.coerce.volume", YSE::INTERNAL::BusValue{1}, YSE::T_GUI);
    CHECK(s.volume() == doctest::Approx(1.0f));
  }

  TEST_CASE("sound bus: anonymous sound is not addressable") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    // No name() call: nothing subscribes, so this publish is a no-op and the
    // volume keeps its post-create default of 0.
    publishFloat("sound..volume", 0.9f);
    CHECK(s.volume() == doctest::Approx(0.0f));
  }

  TEST_CASE("sound bus: destroying a named sound unsubscribes (no effect, no crash)") {
    if (!TestHelpers::engineInit()) return;
    {
      SilentSource src;
      YSE::sound s;
      s.create(src);
      s.name("gone");
      publishFloat("sound.gone.volume", 0.4f);
      CHECK(s.volume() == doctest::Approx(0.4f));
    } // ~sound() unsubscribes and releases the name here

    // Publishing to the dead address must not crash and must reach no one.
    publishFloat("sound.gone.volume", 0.8f);

    // The name is free again: a fresh sound can claim it and receive values.
    SilentSource src2;
    YSE::sound s2;
    s2.create(src2);
    s2.name("gone");
    publishFloat("sound.gone.volume", 0.25f);
    CHECK(s2.volume() == doctest::Approx(0.25f));
  }

  TEST_CASE("sound bus: duplicate name is rejected and the first registration wins") {
    if (!TestHelpers::engineInit()) return;
    SilentSource srcA, srcB;
    YSE::sound a, b;
    a.create(srcA);
    b.create(srcB);

    a.name("dup");
    b.name("dup"); // rejected + logged; a keeps ownership

    publishFloat("sound.dup.volume", 0.6f);
    CHECK(a.volume() == doctest::Approx(0.6f)); // first registration receives
    CHECK(b.volume() == doctest::Approx(0.0f)); // second never subscribed
  }

  TEST_CASE("sound bus: renaming re-subscribes under the new address") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.name("first");
    s.name("second"); // drops "first", claims "second"

    // Old address is dead.
    publishFloat("sound.first.volume", 0.3f);
    CHECK(s.volume() == doctest::Approx(0.0f));

    // New address is live.
    publishFloat("sound.second.volume", 0.7f);
    CHECK(s.volume() == doctest::Approx(0.7f));
  }

  TEST_CASE("sound bus: clearing the name with \"\" makes the sound anonymous") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.name("clearme");
    s.name(""); // unsubscribes, releases the name

    publishFloat("sound.clearme.volume", 0.9f);
    CHECK(s.volume() == doctest::Approx(0.0f));

    // The freed name can be reclaimed by another sound.
    SilentSource src2;
    YSE::sound s2;
    s2.create(src2);
    s2.name("clearme");
    publishFloat("sound.clearme.volume", 0.2f);
    CHECK(s2.volume() == doctest::Approx(0.2f));
  }

  TEST_CASE("sound bus: off-thread volume publish is deferred, never raced onto the SPSC queue") {
    // Regression for issue #193 at the level the bug actually bites: a
    // gMetro-style T_GUI publish arriving on another thread must not run the
    // sound's volume() setter inline — that would push into the sound's
    // single-producer message queue concurrently with the application/control
    // thread and cross-link its block list. The publish is instead deferred
    // and applied on the control thread in drainPending(). Before the fix the
    // volume would already read 0.5 before the drain (dispatched on the worker
    // thread), failing the first check.
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.name("mt193");

    std::thread worker([] {
      YSE::INTERNAL::Bus().publish("sound.mt193.volume", YSE::INTERNAL::BusValue{0.5f}, YSE::T_GUI);
    });
    worker.join();

    // Deferred: not applied on the worker thread.
    CHECK(s.volume() == doctest::Approx(0.0f));

    // Applied on the control (test main) thread when the inbox is drained.
    YSE::INTERNAL::Bus().drainPending();
    CHECK(s.volume() == doctest::Approx(0.5f));

    // `s` is destroyed at scope exit, which unsubscribes and releases the
    // process-global "mt193" name — no shared-state residue for later cases.
  }

} // TEST_SUITE("sound")
