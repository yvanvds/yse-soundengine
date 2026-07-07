// Position-handler tests (issue #170, §8/§10/§11 of
// docs/design/per_note_positioning.md).
//
// Covers the three things the issue names for tests:
//   * handler lifecycle vs voice lifecycle (including stealing) — a note keeps
//     being steered through its release tail, a stolen slot re-inits its
//     handler, and heavy overcommit never crashes or grows the pool;
//   * position trajectories deterministic under seeded random — two synths with
//     the same seed scatter their notes identically, a different seed differs;
//   * controller / aftertouch / handler-param forwarding — the synth hands the
//     live values to the handler through voiceContext, which the built-in orbit
//     handler (aftertouch widens the swarm) and a probe handler read back.
// Plus the epic's acceptance check: N notes trace distinct, moving orbits.
//
// ISOLATION: like `synthpositioning` these drive System::initOffline()/close()
// and so run in their own ctest process (`synthhandlers`), excluded from the
// combined `yse_unit_tests` run. initOffline() needs no audio hardware, so they
// run in CI; a host where it returns false bails the case out as a pass. The
// steal / lifecycle cases are written to run under ASan/TSan via tools/ci-linux/.

#include <doctest/doctest.h>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "sound/soundInterface.hpp"
#include "synth/positionHandler.hpp"
#include "synth/positionHandlers.hpp"
#include "synth/sineVoice.hpp"
#include "synth/synthInterface.hpp"

namespace {

  // Bring a synth (behind a sound) to READY by pumping the offline engine until
  // its voices are cloned on the slow pool, or a deadline passes.
  bool bringToReady(YSE::synth& syn, int expectVoices) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
      YSE::System().update();
      YSE::System().renderOffline(1);
      if (syn.getNumVoices() == expectVoices) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return syn.getNumVoices() == expectVoices;
  }

  // Advance the offline engine by n blocks.
  void pump(int n) {
    for (int b = 0; b < n; ++b) {
      YSE::System().update();
      YSE::System().renderOffline(1);
    }
  }

  // Horizontal (x,z) radius of a position from the origin.
  double planarRadius(const YSE::Pos& p) {
    return std::sqrt(static_cast<double>(p.x) * p.x + static_cast<double>(p.z) * p.z);
  }

  // A handler that echoes voiceContext fields straight into a position, so a
  // test can assert exactly which live value reached the handler. It is also the
  // minimal "write your own handler" example (steers position only, RT-safe).
  class ProbeHandler : public YSE::SYNTH::positionHandler {
  public:
    YSE::SYNTH::positionHandler* clone() override {
      return new ProbeHandler(*this);
    }
    YSE::Pos noteOn(const YSE::SYNTH::voiceContext& ctx) override {
      return probe(ctx);
    }
    YSE::Pos update(const YSE::SYNTH::voiceContext& ctx, Flt) override {
      return probe(ctx);
    }

  private:
    // x <- CC1 (controller forwarding), y <- handler-param 5, z <- aftertouch.
    static YSE::Pos probe(const YSE::SYNTH::voiceContext& ctx) {
      return YSE::Pos(ctx.controller(1) * 10.f, ctx.handlerParam(5), ctx.aftertouch * 4.f);
    }
  };

} // namespace

TEST_SUITE("synthhandlers") {

  // ── Acceptance: N notes trace distinct orbits, and each orbit moves over
  //    time (positions differ per note and advance per block). ──
  TEST_CASE("synthhandlers: swarm notes trace distinct, moving orbits") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice voiceProto;
    voiceProto.attack(0.005f).sustain(0.8f).release(0.2f);
    YSE::SYNTH::orbitHandler orbitProto;
    orbitProto.radius(2.f).velocityRadius(0.f).rate(4.f); // fixed radius so phase alone separates
    {
      YSE::synth syn;
      syn.create().addVoices(voiceProto, 16).positionHandler(orbitProto);
      YSE::sound snd;
      snd.create(syn);
      REQUIRE(bringToReady(syn, 16));
      snd.play();

      const int base = 48;
      const int n = 8;
      for (int i = 0; i < n; ++i)
        syn.noteOn(1, base + i, 0.8f);
      pump(2);

      // Distinct per note: sample each note's position; all pairwise different.
      std::vector<YSE::Pos> first;
      for (int i = 0; i < n; ++i)
        first.push_back(syn.getVoicePosition(1, base + i));
      for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
          CHECK(first[static_cast<size_t>(i)] != first[static_cast<size_t>(j)]);
        }
        // On a fixed-radius orbit every note sits on the same ring.
        CHECK(planarRadius(first[static_cast<size_t>(i)]) == doctest::Approx(2.0).epsilon(0.05));
      }

      // Moving over time: after more blocks each note has advanced its orbit.
      pump(30);
      int moved = 0;
      for (int i = 0; i < n; ++i) {
        if (syn.getVoicePosition(1, base + i) != first[static_cast<size_t>(i)]) ++moved;
      }
      CHECK(moved == n);

      snd.stop();
      pump(4);
    }
    pump(4);
    YSE::System().close();
    CHECK(true);
  }

  // ── Deterministic seeded random spread: two synths seeded the same scatter
  //    their notes identically; a different seed differs. ──
  TEST_CASE("synthhandlers: random spread is deterministic under a seed") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice voiceProto;
    voiceProto.attack(0.005f).sustain(0.8f).release(0.05f);

    YSE::SYNTH::randomSpreadHandler protoA;
    protoA.radius(5.f).seed(1234u);
    YSE::SYNTH::randomSpreadHandler protoB;
    protoB.radius(5.f).seed(1234u); // same seed as A
    YSE::SYNTH::randomSpreadHandler protoC;
    protoC.radius(5.f).seed(9999u); // different seed
    {
      YSE::synth a, b, c;
      a.create().addVoices(voiceProto, 8).positionHandler(protoA);
      b.create().addVoices(voiceProto, 8).positionHandler(protoB);
      c.create().addVoices(voiceProto, 8).positionHandler(protoC);
      YSE::sound sa, sb, sc;
      sa.create(a);
      sb.create(b);
      sc.create(c);
      REQUIRE(bringToReady(a, 8));
      REQUIRE(bringToReady(b, 8));
      REQUIRE(bringToReady(c, 8));
      sa.play();
      sb.play();
      sc.play();

      const int base = 60;
      const int n = 6;
      for (int i = 0; i < n; ++i) {
        a.noteOn(1, base + i, 0.7f);
        b.noteOn(1, base + i, 0.7f);
        c.noteOn(1, base + i, 0.7f);
      }
      pump(2);

      int differsFromC = 0;
      for (int i = 0; i < n; ++i) {
        YSE::Pos pa = a.getVoicePosition(1, base + i);
        YSE::Pos pb = b.getVoicePosition(1, base + i);
        YSE::Pos pc = c.getVoicePosition(1, base + i);
        // Same seed -> identical scatter (same computation, exact match).
        CHECK(pa.x == doctest::Approx(pb.x));
        CHECK(pa.y == doctest::Approx(pb.y));
        CHECK(pa.z == doctest::Approx(pb.z));
        // Draw is inside the radius sphere around the origin centre.
        CHECK(std::sqrt(pa.x * pa.x + pa.y * pa.y + pa.z * pa.z) <= 5.0f * 1.7321f + 1e-3f);
        if (pa != pc) ++differsFromC;
      }
      CHECK(differsFromC > 0); // a different seed produces a different scatter

      a.allNotesOff(0);
      b.allNotesOff(0);
      c.allNotesOff(0);
      pump(4);
    }
    pump(4);
    YSE::System().close();
    CHECK(true);
  }

  // ── Steerable centre: writing handler-params 0..2 recentres the whole spread
  //    on the next block (the swarm-centre use case, §9). ──
  TEST_CASE("synthhandlers: handlerParam recentres the spread") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice voiceProto;
    voiceProto.attack(0.005f).sustain(0.8f).release(0.05f);
    YSE::SYNTH::randomSpreadHandler proto;
    proto.radius(1.f).seed(42u);
    {
      YSE::synth syn;
      syn.create().addVoices(voiceProto, 8).positionHandler(proto);
      YSE::sound snd;
      snd.create(syn);
      REQUIRE(bringToReady(syn, 8));
      snd.play();

      for (int i = 0; i < 4; ++i)
        syn.noteOn(1, 60 + i, 0.7f);
      pump(2);
      YSE::Pos before = syn.getVoicePosition(1, 60);

      // Shift the shared centre far along +X; the held note tracks it next block.
      syn.handlerParam(YSE::SYNTH::HP_CENTER_X, 100.f);
      pump(2);
      YSE::Pos after = syn.getVoicePosition(1, 60);

      CHECK(after.x > before.x + 90.f); // moved by ~the centre shift, radius aside

      snd.stop();
      pump(4);
    }
    pump(4);
    YSE::System().close();
    CHECK(true);
  }

  // ── Controller / aftertouch / handler-param forwarding through voiceContext.
  //    A probe handler echoes each live value into a coordinate. ──
  TEST_CASE("synthhandlers: synth forwards controller/aftertouch to the handler") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice voiceProto;
    voiceProto.attack(0.005f).sustain(0.8f).release(0.05f);
    ProbeHandler probeProto;
    {
      YSE::synth syn;
      syn.create().addVoices(voiceProto, 4).positionHandler(probeProto);
      YSE::sound snd;
      snd.create(syn);
      REQUIRE(bringToReady(syn, 4));
      snd.play();

      syn.noteOn(1, 64, 0.9f);
      pump(2); // establish the voice; live values still at defaults (~0)
      YSE::Pos base = syn.getVoicePosition(1, 64);
      CHECK(base.x == doctest::Approx(0.f).epsilon(0.01));
      CHECK(base.z == doctest::Approx(0.f).epsilon(0.01));

      // CC1 -> x (controller forwarding).
      syn.controller(1, 1, 0.5f);
      // Channel aftertouch -> z (aftertouch forwarding).
      syn.aftertouch(1, -1, 1.0f);
      // Shared handler-param 5 -> y.
      syn.handlerParam(5, 3.f);
      pump(2);

      YSE::Pos p = syn.getVoicePosition(1, 64);
      CHECK(p.x == doctest::Approx(5.f).epsilon(0.01)); // 0.5 * 10
      CHECK(p.y == doctest::Approx(3.f).epsilon(0.01)); // handlerParam(5)
      CHECK(p.z == doctest::Approx(4.f).epsilon(0.01)); // 1.0 * 4

      snd.stop();
      pump(4);
    }
    pump(4);
    YSE::System().close();
    CHECK(true);
  }

  // ── Orbit widens under aftertouch: the built-in handler reads the live value
  //    the synth forwards and spreads the swarm outward. ──
  TEST_CASE("synthhandlers: aftertouch widens the orbit radius") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice voiceProto;
    voiceProto.attack(0.005f).sustain(0.8f).release(0.05f);
    YSE::SYNTH::orbitHandler proto;
    proto.radius(2.f).velocityRadius(0.f).aftertouchWiden(1.f).rate(1.f);
    {
      YSE::synth syn;
      syn.create().addVoices(voiceProto, 4).positionHandler(proto);
      YSE::sound snd;
      snd.create(syn);
      REQUIRE(bringToReady(syn, 4));
      snd.play();

      syn.noteOn(1, 60, 0.5f);
      pump(2);
      double r0 = planarRadius(syn.getVoicePosition(1, 60));
      CHECK(r0 == doctest::Approx(2.0).epsilon(0.05));

      syn.aftertouch(1, 60, 1.0f); // full pressure doubles the radius (widen 1.0)
      pump(2);
      double r1 = planarRadius(syn.getVoicePosition(1, 60));
      CHECK(r1 > r0 + 1.0); // ~4 vs ~2

      snd.stop();
      pump(4);
    }
    pump(4);
    YSE::System().close();
    CHECK(true);
  }

  // ── Handler lifecycle vs voice lifecycle: a released note keeps being steered
  //    through its decay tail (position moves after note-off), then the slot
  //    frees. ──
  TEST_CASE("synthhandlers: handler steers through the release tail") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice voiceProto;
    voiceProto.attack(0.005f).sustain(0.8f).release(0.05f); // audible tail to observe
    YSE::SYNTH::orbitHandler proto;
    proto.radius(3.f).velocityRadius(0.f).rate(6.f);
    {
      YSE::synth syn;
      syn.create().addVoices(voiceProto, 4).positionHandler(proto);
      YSE::sound snd;
      snd.create(syn);
      REQUIRE(bringToReady(syn, 4));
      snd.play();

      syn.noteOn(1, 55, 0.8f);
      pump(2);
      YSE::Pos held = syn.getVoicePosition(1, 55);
      CHECK(planarRadius(held) == doctest::Approx(3.0).epsilon(0.05));

      // Release: the note enters its tail but keeps orbiting (still sounding).
      syn.noteOff(1, 55);
      pump(1);
      YSE::Pos releasing = syn.getVoicePosition(1, 55);
      CHECK(releasing != held); // moved through the tail
      CHECK(planarRadius(releasing) == doctest::Approx(3.0).epsilon(0.1)); // still on the ring

      // Let the tail finish; the slot then frees (no voice reports that note).
      bool freed = false;
      for (int i = 0; i < 200 && !freed; ++i) {
        pump(1);
        if (syn.getVoicePosition(1, 55) == YSE::Pos(0.f)) freed = true;
      }
      CHECK(freed);

      snd.stop();
      pump(4);
    }
    pump(4);
    YSE::System().close();
    CHECK(true);
  }

  // ── Voice stealing re-inits the handler: a tiny pool massively overcommitted
  //    steals every block; handlers re-seed per steal, the pool never grows, and
  //    positions stay finite. The ASan/TSan target for handler lifetime. ──
  TEST_CASE("synthhandlers: stealing re-inits handlers without leaks") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice voiceProto;
    voiceProto.attack(0.001f).decay(0.002f).sustain(0.8f).release(0.05f);
    YSE::SYNTH::orbitHandler proto;
    proto.radius(1.f).velocityRadius(3.f).rate(5.f);
    {
      YSE::synth syn;
      syn.create().addVoices(voiceProto, 4).positionHandler(proto); // tiny pool
      YSE::sound snd;
      snd.create(syn);
      REQUIRE(bringToReady(syn, 4));
      snd.play();

      for (int n = 40; n < 64; ++n) // 24 keys vs 4 voices -> constant stealing
        syn.noteOn(1, n, 0.8f);
      for (int b = 0; b < 64; ++b) {
        YSE::System().update();
        YSE::System().renderOffline(1);
        CHECK(syn.getNumVoices() == 4);
        if (b % 8 == 0) {
          for (int n = 40; n < 64; ++n)
            syn.noteOn(1, n, 0.7f);
        }
        // Whatever note currently owns a slot must have a finite position.
        for (int n = 40; n < 64; ++n) {
          YSE::Pos p = syn.getVoicePosition(1, n);
          CHECK(std::isfinite(p.x));
          CHECK(std::isfinite(p.y));
          CHECK(std::isfinite(p.z));
        }
      }
      syn.allNotesOff(0);
      pump(8);
      snd.stop();
      pump(4);
    }
    pump(8);
    YSE::System().close();
    CHECK(true);
  }

  // ── No handler attached: the synth is unchanged — every voice stays at the
  //    aggregate origin (strictly additive, §8). ──
  TEST_CASE("synthhandlers: no handler leaves voices at the aggregate position") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice voiceProto;
    voiceProto.attack(0.005f).sustain(0.8f).release(0.05f);
    {
      YSE::synth syn;
      syn.create().addVoices(voiceProto, 4); // no positionHandler()
      YSE::sound snd;
      snd.create(syn);
      REQUIRE(bringToReady(syn, 4));
      snd.play();

      syn.noteOn(1, 60, 0.8f);
      pump(4);
      CHECK(syn.getVoicePosition(1, 60) == YSE::Pos(0.f));

      // notePosition() still works with no handler (app-driven placement).
      syn.notePosition(1, 60, YSE::Pos(7.f, 0.f, -3.f));
      pump(2);
      YSE::Pos p = syn.getVoicePosition(1, 60);
      CHECK(p.x == doctest::Approx(7.f));
      CHECK(p.z == doctest::Approx(-3.f));

      snd.stop();
      pump(4);
    }
    pump(4);
    YSE::System().close();
    CHECK(true);
  }

} // TEST_SUITE("synthhandlers")
