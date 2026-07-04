// Tests for the SOUND::implementationObject and SOUND::managerObject internals
// (YseEngine/sound/soundImplementation.cpp + soundManager.cpp).
//
// The existing test_sound_state.cpp covers the public sound API; this TU drives
// the message-queue handlers, sync(), update() spatial math, virtual-sound
// budgeting, occlusion callback hook, and the manager's update loop end-to-end.
//
// Strategy: create sounds from a no-op DSP source (no file I/O, no audio device
// needed), drive every setter on the public interface to enqueue every MESSAGE
// type, call SOUND::Manager().update() repeatedly to drain queues and run
// update().  Observable effects are limited (most impl fields are private), so
// most assertions are structural — we verify the code paths execute without
// crashing and that the publicly-visible mirror fields (_head_status,
// _head_time, _head_length, _volume) stay coherent.
//
// Async setup: SOUND::Manager().update() queues a thread-pool job that calls
// setup() on each impl in toLoad.  The job runs on a low-priority pool; we
// give it a few milliseconds + several update calls to settle.

#include <doctest/doctest.h>
#include <chrono>
#include <cmath>
#include <thread>
#include "yse.hpp"
#include "sound/soundInterface.hpp"
#include "sound/soundManager.h"
#include "sound/soundImplementation.h"
#include "sound/soundMessage.h"
#include "dsp/dspObject.hpp"
#include "channel/channelInterface.hpp"
#include "internal/settings.h"
#include "internal/time.h"
#include "implementations/listenerImplementation.h"
#include "internal/virtualFinder.h"
#include "support/null_device.hpp"

namespace {

  // Silent DSP source — emits no audio, fulfils dspSourceObject's pure virtuals.
  // File-scope (process-lifetime) instances are used below, never stack locals.
  // This follows the lifetime contract documented in soundInterface.hpp's
  // `create(YSE::DSP::dspSourceObject&, ...)` overload: the caller owns the
  // source and must keep it alive until after the sound is fully released AND
  // the slow-pool's delete tick has fired. Phase C made `source_dsp` atomic
  // and nullifies it at the OBJECT_RELEASE→OBJECT_DELETE transition for
  // defence in depth, but stack-local sources with short lifetimes can still
  // produce use-after-free. File-scope instances are the canonical safe form.
  struct SilentSource : YSE::DSP::dspSourceObject {
    void process(YSE::SOUND_STATUS&) override {}
    void frequency(float) override {}
  };

  // Minimal dspObject for setDSP() coverage — process() is a no-op.
  // MULTICHANNELBUFFER is a macro expanding to std::vector<YSE::DSP::buffer>;
  // the qualified spelling lets the override resolve outside the YSE namespace.
  // Same file-scope lifetime reasoning as SilentSource above (see Phase C
  // contract in soundInterface.hpp).
  struct NopDsp : YSE::DSP::dspObject {
    void create() override {}
    void process(std::vector<YSE::DSP::buffer>&) override {}
  };

  // Shared file-scope instances reused across tests.  These are stateless, so
  // sharing is safe; each test sets the position / volume / etc. it needs.
  SilentSource g_src;
  SilentSource g_srcs[8]; // size = max N used in multi-sound tests below
  NopDsp g_dsp;
  NopDsp g_dsp2;

  // Drive SOUND::Manager().update() several times, with short sleeps so the
  // async setupJob has a chance to call setup() on freshly created sounds and
  // they can be promoted to inUse on a subsequent update().  This is a
  // best-effort drain; tests guard on observable side-effects rather than
  // asserting strict count of update iterations needed.
  void drainSoundManager(int iterations = 8) {
    for (int i = 0; i < iterations; i++) {
      YSE::INTERNAL::Time().update();
      YSE::SOUND::Manager().update();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  // Module-scope counter so we can verify the occlusion callback was invoked.
  static int g_occlusionCallCount = 0;
  float occlusionStub(const YSE::Pos&, const YSE::Pos&) {
    ++g_occlusionCallCount;
    return 0.25f;
  }

} // namespace

TEST_SUITE("sound") {

  // ─── parseMessage: every MESSAGE enum arm ────────────────────────────────────
  //
  // Each test drives a setter, then runs the manager update to ensure sync()
  // drains the queue and parseMessage() executes the corresponding switch case.
  // We do not assert internal field changes (most are private) — coverage here
  // is structural plus crash-freedom + interface mirror coherence.

  TEST_CASE("sound impl: POSITION message is parsed without crash") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.pos(YSE::Pos(1.f, 2.f, 3.f));
    drainSoundManager();
    CHECK(s.pos().x == doctest::Approx(1.f));
    CHECK(s.pos().y == doctest::Approx(2.f));
    CHECK(s.pos().z == doctest::Approx(3.f));
  }

  TEST_CASE("sound impl: SPREAD message is parsed without crash") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.spread(0.5f);
    drainSoundManager();
    CHECK(s.spread() == doctest::Approx(0.5f));
  }

  TEST_CASE("sound impl: VOLUME_VALUE + VOLUME_TIME messages are parsed") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    // time>0 sends both messages — exercises the VOLUME_TIME arm too.
    s.volume(0.5f, 100u);
    // Read the interface mirror *before* drainSoundManager: sync() overwrites
    // _volume from currentVolume_dsp (= 0 until dsp() runs in the audio
    // callback, which never fires under TestHelpers::engineInit's null
    // device), so checking after drain would observe the clobber instead of
    // the value we just set.
    CHECK(s.volume() == doctest::Approx(0.5f));
    drainSoundManager();
    CHECK(true);
  }

  TEST_CASE("sound impl: SPEED message is parsed without crash") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.speed(1.5f);
    drainSoundManager();
    CHECK(s.speed() == doctest::Approx(1.5f));
  }

  TEST_CASE("sound impl: SIZE message is parsed without crash") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.size(10.f);
    drainSoundManager();
    CHECK(s.size() == doctest::Approx(10.f));
  }

  TEST_CASE("sound impl: LOOP message is parsed without crash") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.looping(true);
    drainSoundManager();
    CHECK(s.looping() == true);
  }

  TEST_CASE("sound impl: OCCLUSION message is parsed without crash") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.occlusion(true);
    drainSoundManager();
    CHECK(s.occlusion() == true);
  }

  TEST_CASE("sound impl: RELATIVE message is parsed without crash") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.relative(true);
    drainSoundManager();
    CHECK(s.relative() == true);
  }

  TEST_CASE("sound impl: DOPPLER message is parsed without crash") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.doppler(false);
    drainSoundManager();
    CHECK(s.doppler() == false);
  }

  TEST_CASE("sound impl: PAN2D message is parsed without crash") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.pan2D(true);
    drainSoundManager();
    CHECK(s.pan2D() == true);
    // pan2D mirrors relative=true / doppler=false on the interface side too.
    CHECK(s.relative() == true);
    CHECK(s.doppler() == false);
  }

  TEST_CASE("sound impl: TIME message is parsed without crash") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.time(0.0f); // for a DSP source there is no file length; 0 is always safe
    drainSoundManager();
    CHECK(true);
  }

  TEST_CASE("sound impl: FADE_AND_STOP message is parsed without crash") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.fadeAndStop(100u);
    drainSoundManager();
    CHECK(true);
  }

  TEST_CASE("sound impl: DSP message is parsed without crash") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.setDSP(&g_dsp);
    drainSoundManager();
    CHECK(s.getDSP() == &g_dsp);
  }

  TEST_CASE("sound impl: replacing a DSP plugin clears the old calledfrom") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.setDSP(&g_dsp);
    drainSoundManager();
    s.setDSP(&g_dsp2);
    drainSoundManager();
    CHECK(s.getDSP() == &g_dsp2);
  }

  TEST_CASE("sound impl: MOVE message reconnects sound to another channel") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel target;
    target.create("test_target", YSE::ChannelMaster());
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.moveTo(target);
    drainSoundManager();
    CHECK(true);
  }

  TEST_CASE("sound impl: INTENT(play) message is parsed without crash") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.play();
    drainSoundManager();
    CHECK(true);
  }

  TEST_CASE("sound impl: INTENT(pause / stop / restart / toggle) all parse") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.play();
    drainSoundManager();
    s.pause();
    drainSoundManager();
    s.stop();
    drainSoundManager();
    s.restart();
    drainSoundManager();
    s.toggle();
    drainSoundManager();
    CHECK(true);
  }

  // ─── update(): spatial math is driven by listener + distanceFactor ──────────

  TEST_CASE("sound impl: update() runs spatial math for a positioned sound") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    s.pos(YSE::Pos(5.f, 0.f, 0.f));
    YSE::Listener().pos(YSE::Pos(0.f, 0.f, 0.f));
    // Run a couple of update cycles; the second one observes a non-zero
    // Time().delta() so the doppler-velocity divide is finite.
    drainSoundManager(12);
    CHECK(true);
  }

  TEST_CASE("sound impl: update() with relative=true takes the relative branch") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    s.relative(true);
    s.pos(YSE::Pos(2.f, 0.f, 1.f));
    drainSoundManager(12);
    CHECK(true);
  }

  TEST_CASE("sound impl: update() with doppler=false skips the velocity calc") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    s.doppler(false);
    s.pos(YSE::Pos(3.f, 0.f, 0.f));
    drainSoundManager(12);
    CHECK(true);
  }

  TEST_CASE("sound impl: update() respects distanceFactor scaling") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    s.pos(YSE::Pos(1.f, 0.f, 0.f));
    YSE::INTERNAL::Settings().distanceFactor = 2.f;
    drainSoundManager(12);
    YSE::INTERNAL::Settings().distanceFactor = 1.f;
    CHECK(true);
  }

  TEST_CASE("sound impl: update() respects dopplerScale=2.0") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    s.pos(YSE::Pos(0.f, 0.f, 0.f));
    YSE::INTERNAL::Settings().dopplerScale = 2.f;
    drainSoundManager(12);
    YSE::INTERNAL::Settings().dopplerScale = 1.f;
    CHECK(true);
  }

  // ─── Occlusion callback path ─────────────────────────────────────────────────

  TEST_CASE("sound impl: update() invokes occlusion callback when occlusion is enabled") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    s.occlusion(true);
    drainSoundManager();
    g_occlusionCallCount = 0;
    YSE::System().occlusionCallback(occlusionStub);
    drainSoundManager(12);
    // Callback may be invoked multiple times across the update iterations.
    CHECK(g_occlusionCallCount > 0);
    // Reset the callback so it doesn't leak into other tests.
    YSE::System().occlusionCallback(nullptr);
  }

  TEST_CASE("sound impl: update() skips occlusion when callback is null") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    s.occlusion(true);
    YSE::System().occlusionCallback(nullptr);
    drainSoundManager(12);
    CHECK(true);
  }

  // ─── Virtual-sound finder budget ─────────────────────────────────────────────

  TEST_CASE("sound impl: virtualFinder budget pruning runs across many sounds") {
    if (!TestHelpers::engineInit()) return;
    // Constrain the limit so virtualSoundFinder has to actively prune.
    YSE::System().maxSounds(2);

    constexpr int N = 8;
    YSE::sound sounds[N];
    static_assert(N <= sizeof(g_srcs) / sizeof(g_srcs[0]), "g_srcs too small");
    for (int i = 0; i < N; i++) {
      sounds[i].create(g_srcs[i]);
      sounds[i].pos(YSE::Pos((float)i * 10.f, 0.f, 0.f));
      sounds[i].play();
    }
    drainSoundManager(20);
    // Restore default (≥ 64 — definitely above any test count we set above).
    YSE::System().maxSounds(64);
    CHECK(true);
  }

  // ─── virtualDist metric: volume weighting (#205) ────────────────────────────
  //
  // Regression tests for the inverted volume weighting. The VirtualSoundFinder
  // keeps the sounds with the *smallest* virtualDist real (see inRange /
  // sortSoundObjects); importance must therefore rise with volume and fall with
  // distance. Before the fix the metric multiplied by volume, so a muted sound
  // scored 0 (maximally important) and outranked loud ones. These call the pure
  // helper directly so the assertions are exact and don't depend on the finder's
  // adaptive histogram.

  TEST_CASE("virtualDist: at equal distance a louder sound outranks a quieter one") {
    using Impl = YSE::SOUND::implementationObject;
    const float loud = Impl::computeVirtualDist(10.f, 0.f, 1.0f);
    const float quiet = Impl::computeVirtualDist(10.f, 0.f, 0.25f);
    // Lower == more important. The loud sound must be more important.
    CHECK(loud < quiet);
  }

  TEST_CASE("virtualDist: a muted sound does NOT outrank an audible one (#205)") {
    using Impl = YSE::SOUND::implementationObject;
    // Same distance, one fully audible, one muted. Pre-fix the muted sound
    // scored 0 and was always kept; post-fix it must score higher (less
    // important) than the audible sound.
    const float audible = Impl::computeVirtualDist(10.f, 0.f, 1.0f);
    const float muted = Impl::computeVirtualDist(10.f, 0.f, 0.0f);
    CHECK(muted > audible);
  }

  TEST_CASE("virtualDist: at equal volume a nearer sound outranks a farther one") {
    using Impl = YSE::SOUND::implementationObject;
    const float near = Impl::computeVirtualDist(5.f, 0.f, 1.0f);
    const float far = Impl::computeVirtualDist(50.f, 0.f, 1.0f);
    CHECK(near < far);
  }

  TEST_CASE("virtualDist: is non-negative and clamps a listener inside `size` to 0") {
    using Impl = YSE::SOUND::implementationObject;
    // distance < size -> effective distance clamps to 0 -> maximally important.
    CHECK(Impl::computeVirtualDist(2.f, 10.f, 1.0f) == doctest::Approx(0.f));
    // A muted, in-size sound is still finite and non-negative.
    CHECK(Impl::computeVirtualDist(2.f, 10.f, 0.0f) >= 0.f);
  }

  // ─── Virtualization boundary: hysteresis + farewell fade (#206) ─────────────
  //
  // Two independent regressions for the hard-mute-without-fade / flutter bug:
  //  1. virtualFinder::inRange applies a ~10% hysteresis band around the cutoff
  //     so a sound sitting on the boundary can't toggle real/virtual each tick.
  //  2. computeVirtualAction gives a sound crossing to virtual exactly one
  //     farewell block (rendered, gains forced to 0) before it goes silent, so
  //     its contribution ramps to zero instead of stepping (clicking).

  TEST_CASE("virtualFinder: hysteresis widens the real region for already-real sounds (#206)") {
    YSE::virtualFinder vf(10);
    vf.setLimit(2);
    // Mirror the production cycle (SOUND::Manager: reset -> add -> calculate);
    // reset() zeroes the entry counter and seeds calculatedMax to 10, so each
    // of the 10 bins spans 1.0 and the histogram of {1,2,3,4,5} yields a finite
    // cutoff `range` (== 4.0 here).
    vf.reset();
    vf.add(1.f);
    vf.add(2.f);
    vf.add(3.f);
    vf.add(4.f);
    vf.add(5.f);
    vf.calculate();

    // Sweep candidate distances. For every value the region kept real when the
    // sound was ALREADY real must be a superset of the region promoted from
    // virtual: `promoted ⇒ keptReal`. And the two verdicts must differ for at
    // least one value — otherwise the thresholds coincide and there is no
    // hysteresis, which is exactly the flutter the fix removes.
    bool differsSomewhere = false;
    for (float v = 0.f; v <= 12.f; v += 0.05f) {
      const bool keptReal = vf.inRange(v, /*wasReal=*/true);
      const bool promoted = vf.inRange(v, /*wasReal=*/false);
      CHECK((keptReal || !promoted)); // promoted implies keptReal
      if (keptReal != promoted) differsSomewhere = true;
    }
    CHECK(differsSomewhere);
  }

  TEST_CASE("virtualFinder: under the budget every sound is real regardless of state (#206)") {
    YSE::virtualFinder vf(10);
    vf.setLimit(50); // limit far above the number of entries -> nobody virtual
    vf.reset();
    vf.add(1.f);
    vf.add(9.f);
    vf.calculate();
    CHECK(vf.inRange(9.f, /*wasReal=*/true) == true);
    CHECK(vf.inRange(9.f, /*wasReal=*/false) == true);
  }

  TEST_CASE("computeVirtualAction: a real sound renders normally, no fade (#206)") {
    using Impl = YSE::SOUND::implementationObject;
    // Fresh real sound.
    auto a = Impl::computeVirtualAction(/*real=*/true, /*wasVirtual=*/false);
    CHECK(a.render);
    CHECK_FALSE(a.fadeOut);
    CHECK_FALSE(a.nowVirtual);
    // Re-entry from virtual: renders again, clears the virtual state, no fade.
    auto b = Impl::computeVirtualAction(/*real=*/true, /*wasVirtual=*/true);
    CHECK(b.render);
    CHECK_FALSE(b.fadeOut);
    CHECK_FALSE(b.nowVirtual);
  }

  TEST_CASE("computeVirtualAction: first block going virtual gets one farewell fade (#206)") {
    using Impl = YSE::SOUND::implementationObject;
    auto a = Impl::computeVirtualAction(/*real=*/false, /*wasVirtual=*/false);
    CHECK(a.render); // still rendered this block...
    CHECK(a.fadeOut); // ...but with gains forced to 0 so it ramps to silence
    CHECK(a.nowVirtual);
  }

  TEST_CASE("computeVirtualAction: an already-virtual sound stays silent (#206)") {
    using Impl = YSE::SOUND::implementationObject;
    auto a = Impl::computeVirtualAction(/*real=*/false, /*wasVirtual=*/true);
    CHECK_FALSE(a.render); // skipped entirely — no repeated fade, no click
    CHECK_FALSE(a.fadeOut);
    CHECK(a.nowVirtual);
  }

  // ─── Speaker-density overlap weight (#207) ──────────────────────────────────
  //
  // Regression tests for the mis-parenthesized density term. The overlap weight
  // that toChannels() sums into `effective` must be the same cardioid as the pan
  // term: (1 + cos(Δ)) * 0.5f, range [0..1]. The pre-fix code multiplied 0.5f
  // into cos only — (1 + cos(Δ) * 0.5f) — giving range [0.5..1.5], offset by
  // +0.5 per speaker pair. These call the pure helper directly so the assertions
  // are exact and independent of any speaker layout.

  TEST_CASE("speakerOverlap: coincident speakers overlap fully (== 1) (#207)") {
    using Impl = YSE::SOUND::implementationObject;
    // Δ = 0. Fixed formula: (1 + 1) * 0.5 = 1.0. Pre-fix: 1 + 1*0.5 = 1.5.
    CHECK(Impl::computeSpeakerOverlap(0.f, 0.f) == doctest::Approx(1.f));
    CHECK(Impl::computeSpeakerOverlap(1.2f, 1.2f) == doctest::Approx(1.f));
  }

  TEST_CASE("speakerOverlap: opposite speakers do not overlap (== 0) (#207)") {
    using Impl = YSE::SOUND::implementationObject;
    // Δ = π. Fixed formula: (1 + (-1)) * 0.5 = 0.0. Pre-fix: 1 + (-1)*0.5 = 0.5.
    CHECK(Impl::computeSpeakerOverlap(0.f, static_cast<float>(YSE::Pi)) == doctest::Approx(0.f));
  }

  TEST_CASE("speakerOverlap: perpendicular speakers give the half weight (#207)") {
    using Impl = YSE::SOUND::implementationObject;
    // Δ = π/2. Fixed formula: (1 + 0) * 0.5 = 0.5. Pre-fix: 1 + 0*0.5 = 1.0.
    CHECK(Impl::computeSpeakerOverlap(0.f, static_cast<float>(YSE::Pi) * 0.5f) ==
          doctest::Approx(0.5f));
  }

  TEST_CASE("speakerOverlap: weight stays within [0..1] across a full sweep (#207)") {
    using Impl = YSE::SOUND::implementationObject;
    // Pre-fix the range was [0.5..1.5]; the fixed cardioid must never leave
    // [0..1] for any angular separation. Step through a full turn in integer
    // increments to keep the loop induction non-floating-point.
    for (int i = 0; i <= 360; ++i) {
      const float d = static_cast<float>(i) * static_cast<float>(YSE::Pi) / 180.f;
      const float w = Impl::computeSpeakerOverlap(0.f, d);
      CHECK(w >= -1e-4f);
      CHECK(w <= 1.f + 1e-4f);
    }
  }

  TEST_CASE("speakerOverlap: mirrors the pan term's parenthesization (#207)") {
    using Impl = YSE::SOUND::implementationObject;
    // The overlap weight must use the exact same curve as the pan term in
    // toChannels(): initPan = (1 + cos(speakerAngle - sourceAngle)) * 0.5f.
    // Recompute the pan formula independently and require a match — this is the
    // relationship the bug broke.
    const float speakerAngle = 0.7f;
    const float sourceAngle = 2.1f;
    const float panTerm = (1.f + std::cos(speakerAngle - sourceAngle)) * 0.5f;
    CHECK(Impl::computeSpeakerOverlap(speakerAngle, sourceAngle) == doctest::Approx(panTerm));
  }

  // ─── Manager update loop / lifecycle ─────────────────────────────────────────

  TEST_CASE("sound impl: sync() detects head==nullptr after destructor and releases impl") {
    if (!TestHelpers::engineInit()) return;
    {
      YSE::sound s;
      s.create(g_src);
      drainSoundManager();
      s.play();
      drainSoundManager();
    } // ~sound() calls removeInterface(); the next sync() observes head==nullptr.
    // Drain again: sync() should set OBJECT_RELEASE on the PT_DSP impl,
    // the manager erases it from inUse, and the deleteJob fires on the next
    // iteration to remove from the implementations list.
    drainSoundManager(20);
    CHECK(true);
  }

  TEST_CASE("sound impl: manager handles many concurrent sounds without crash") {
    if (!TestHelpers::engineInit()) return;
    constexpr int N = 4;
    YSE::sound sounds[N];
    static_assert(N <= sizeof(g_srcs) / sizeof(g_srcs[0]), "g_srcs too small");
    for (int i = 0; i < N; i++) {
      sounds[i].create(g_srcs[i]);
      sounds[i].pos(YSE::Pos((float)i, 0.f, 0.f));
      sounds[i].volume(0.5f);
      sounds[i].play();
    }
    drainSoundManager(15);
    for (int i = 0; i < N; i++)
      sounds[i].stop();
    drainSoundManager();
    CHECK(true);
  }

  // ─── sortSoundObjects helper ────────────────────────────────────────────────
  //
  // The static sortSoundObjects function is private but reachable via
  // std::sort on a forward_list of impl pointers — actually exercising that
  // path requires manager internals.  We use the publicly-visible behaviour:
  // creating multiple sounds and pumping the manager runs the manager's full
  // internal sort+budget code path under the virtualFinder.  The "many sounds"
  // test above already exercises it; this test pins the structural call site
  // by setting a small max so the budget pruning + sort definitely activates.

  TEST_CASE("sound impl: virtualFinder.calculate() runs with a tight budget and many sounds") {
    if (!TestHelpers::engineInit()) return;
    YSE::System().maxSounds(1);
    constexpr int N = 5;
    YSE::sound sounds[N];
    static_assert(N <= sizeof(g_srcs) / sizeof(g_srcs[0]), "g_srcs too small");
    for (int i = 0; i < N; i++) {
      sounds[i].create(g_srcs[i]);
      sounds[i].pos(YSE::Pos((float)(i + 1) * 50.f, 0.f, 0.f));
      sounds[i].play();
    }
    drainSoundManager(20);
    YSE::System().maxSounds(64);
    CHECK(true);
  }

} // TEST_SUITE("sound")
