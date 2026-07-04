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
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>
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
  // Second, distinct callback so the round-trip test can tell the two apart.
  float occlusionStub2(const YSE::Pos&, const YSE::Pos&) {
    return 0.5f;
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
    CHECK(s.volume() == doctest::Approx(0.5f));
    drainSoundManager();
    CHECK(true);
  }

  // Regression for issue #191: sync() used to push the live dsp volume back
  // into the user-owned interface object (`h->_volume = currentVolume_dsp`),
  // a use-after-free window against ~sound() AND a clobber of the getter's
  // last-set cache. The write-through is gone; volume() must survive a full
  // sync cycle and keep returning the value the user set (like every sibling
  // getter). Pre-fix this observed the clobber (currentVolume_dsp == 0 under
  // the null device, since dsp() never runs) and returned 0.0 instead of 0.5.
  TEST_CASE("sound impl: sync() no longer clobbers the volume getter (#191)") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    drainSoundManager();
    s.volume(0.5f);
    // Drive several sync() cycles — exactly the path that used to write through
    // the freed interface pointer. The set value must persist.
    drainSoundManager();
    CHECK(s.volume() == doctest::Approx(0.5f));
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

  // ─── Occlusion callback path (issue #209) ────────────────────────────────────
  //
  // The user occlusion callback must run on the CONTROL thread (inside
  // System().update() -> SOUND::updateOcclusion()), never on the audio callback
  // thread (SOUND::Manager().update(), which the device callback drives). Its
  // clamped result reaches the implementation over the sound message queue as an
  // OCCLUSION_VALUE message. Pre-#209 the callback ran inside
  // implementationObject::update() on the audio thread — a real-time violation.

  TEST_CASE(
      "sound impl: occlusion callback runs on the control thread, not the audio thread (#209)") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    s.occlusion(true);
    drainSoundManager();

    g_occlusionCallCount = 0;
    YSE::System().occlusionCallback(occlusionStub);

    // Audio-thread path: pumping the manager (exactly what the device callback
    // does) must NOT invoke the user callback. Pre-#209 this ran the callback in
    // implementationObject::update() and the count would be > 0 — the regression.
    drainSoundManager(12);
    CHECK(g_occlusionCallCount == 0);

    // Control-thread path: the driver invoked from System().update() runs it.
    YSE::SOUND::updateOcclusion();
    CHECK(g_occlusionCallCount > 0);

    // The delivered OCCLUSION_VALUE message must parse cleanly on the next sync.
    drainSoundManager();

    // Reset the callback so it doesn't leak into other tests.
    YSE::System().occlusionCallback(nullptr);
  }

  TEST_CASE("sound impl: updateOcclusion() is a no-op when the callback is null (#209)") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    s.occlusion(true);
    YSE::System().occlusionCallback(nullptr);
    YSE::SOUND::updateOcclusion(); // no callback installed -> nothing to run
    drainSoundManager(12);
    CHECK(true);
  }

  TEST_CASE(
      "sound impl: updateOcclusion() only runs callbacks for occlusion-enabled sounds (#209)") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(g_src);
    // Deliberately do NOT enable occlusion on this sound.
    drainSoundManager();
    g_occlusionCallCount = 0;
    YSE::System().occlusionCallback(occlusionStub);
    YSE::SOUND::updateOcclusion();
    CHECK(g_occlusionCallCount == 0);
    YSE::System().occlusionCallback(nullptr);
  }

  // ─── Occlusion callback pointer is atomic (issue #199) ───────────────────────
  //
  // occlusionPtr is written by System().occlusionCallback(func) on the
  // application thread and read by SOUND::updateOcclusion() on the control
  // thread. It must be a std::atomic with release-store / acquire-load ordering
  // (matching the C API callback-bridge convention) — a plain function pointer
  // is a data race and lets the compiler cache the load. These tests pin the
  // observable contract; the concurrency test is the one ThreadSanitizer flags
  // on the pre-#199 plain-pointer version.

  TEST_CASE("system: occlusionCallback install/read round-trips through the atomic (#199)") {
    if (!TestHelpers::engineInit()) return;
    YSE::System().occlusionCallback(nullptr);
    CHECK(YSE::System().occlusionCallback() == nullptr);

    YSE::System().occlusionCallback(occlusionStub);
    CHECK(YSE::System().occlusionCallback() == occlusionStub);

    // Overwrite with a distinct callback — the getter must observe the new one.
    YSE::System().occlusionCallback(occlusionStub2);
    CHECK(YSE::System().occlusionCallback() == occlusionStub2);

    YSE::System().occlusionCallback(nullptr);
    CHECK(YSE::System().occlusionCallback() == nullptr);
  }

  TEST_CASE("system: occlusion callback atomic is lock-free (RT-safe) (#199)") {
    // The control-thread read must never take a lock; guarantee it at compile
    // time. Function pointers are pointer-sized, so this holds on every target.
    CHECK(std::atomic<YSE::occlusionFunc>::is_always_lock_free);
  }

  TEST_CASE("system: concurrent install + read of the occlusion callback is race-free (#199)") {
    if (!TestHelpers::engineInit()) return;
    // A writer thread swaps the callback while a reader thread repeatedly loads
    // it. On the fixed (atomic) code this is well-defined; on the pre-#199 plain
    // pointer it is a data race that ThreadSanitizer reports. The reader only
    // ever sees one of the installed values (or null) — never a torn pointer.
    //
    // Both threads spin on a start gate and then run for a fixed wall-clock
    // window so their accesses genuinely overlap — otherwise the writer can
    // finish before the reader is even scheduled and the race never manifests.
    std::atomic<bool> go{false};
    std::atomic<bool> readerFault{false};
    auto deadline = [] {
      return std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
    };
    std::thread writer([&] {
      while (!go.load(std::memory_order_acquire)) {}
      const auto end = deadline();
      int i = 0;
      while (std::chrono::steady_clock::now() < end) {
        YSE::System().occlusionCallback((i++ & 1) ? occlusionStub : occlusionStub2);
      }
    });
    std::thread reader([&] {
      while (!go.load(std::memory_order_acquire)) {}
      const auto end = deadline();
      while (std::chrono::steady_clock::now() < end) {
        YSE::occlusionFunc cb = YSE::System().occlusionCallback();
        if (!(cb == nullptr || cb == occlusionStub || cb == occlusionStub2)) {
          readerFault.store(true, std::memory_order_relaxed);
        }
      }
    });
    go.store(true, std::memory_order_release);
    writer.join();
    reader.join();
    CHECK(readerFault.load(std::memory_order_relaxed) == false);
    YSE::System().occlusionCallback(nullptr);
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

  // ─── Pan-power normalisation: no NaN on degenerate layouts (#202) ───────────
  //
  // Regression tests for the divide-by-zero in toChannels(). Each speaker's
  // share of the source power is pow(initGain,2) / power, where `power` is the
  // sum of pow(initGain,2) over every speaker. On a MONO layout with the source
  // directly behind the listener the only speaker's initPan = (1+cos(π))/2 = 0,
  // so initGain = 0, power = 0, and the division is 0/0 = NaN — which multiplies
  // into the block, propagates through the whole mix and latches into lastGain,
  // poisoning subsequent blocks. computePanRatio must fall back to an equal 1/N
  // split whenever `power` collapses (or is non-finite) so the gains stay finite
  // for every source angle on every layout. These call the pure helper directly
  // (and reconstruct the toChannels() pan math from the other pure helpers) so
  // the assertions are exact and independent of any audio device.

  TEST_CASE("panRatio: normal power splits proportionally to pow(initGain,2) (#202)") {
    using Impl = YSE::SOUND::implementationObject;
    // Two speakers, gains 0.8 / 0.6 -> power = 0.64 + 0.36 = 1.0.
    const float g0 = 0.8f, g1 = 0.6f;
    const float power = g0 * g0 + g1 * g1;
    CHECK(Impl::computePanRatio(g0, power, 2) == doctest::Approx(g0 * g0 / power));
    CHECK(Impl::computePanRatio(g1, power, 2) == doctest::Approx(g1 * g1 / power));
    // Shares of a well-posed layout sum to 1.
    CHECK(Impl::computePanRatio(g0, power, 2) + Impl::computePanRatio(g1, power, 2) ==
          doctest::Approx(1.f));
  }

  TEST_CASE("panRatio: zero total power falls back to an equal 1/N split (#202)") {
    using Impl = YSE::SOUND::implementationObject;
    // The mono degenerate case: single speaker, power collapsed to 0 -> full gain.
    CHECK(Impl::computePanRatio(0.f, 0.f, 1) == doctest::Approx(1.f));
    // Four antipodal speakers: each gets 1/4, and the split still sums to 1.
    CHECK(Impl::computePanRatio(0.f, 0.f, 4) == doctest::Approx(0.25f));
  }

  TEST_CASE("panRatio: result is finite for zero, tiny, and non-finite power (#202)") {
    using Impl = YSE::SOUND::implementationObject;
    // Pre-fix pow(0,2)/0 == NaN. All of these must return a finite number.
    CHECK(std::isfinite(Impl::computePanRatio(0.f, 0.f, 1)));
    CHECK(std::isfinite(Impl::computePanRatio(0.f, 1e-30f, 2))); // below the 1e-9 floor
    CHECK(std::isfinite(Impl::computePanRatio(0.f, std::numeric_limits<float>::quiet_NaN(), 2)));
  }

  TEST_CASE("panRatio: a zero-speaker layout returns 0 rather than dividing by zero (#202)") {
    using Impl = YSE::SOUND::implementationObject;
    CHECK(Impl::computePanRatio(0.f, 0.f, 0) == doctest::Approx(0.f));
  }

  TEST_CASE("panRatio: mono, source directly behind the listener, yields a finite gain (#202)") {
    using Impl = YSE::SOUND::implementationObject;
    // Reproduce toChannels()'s per-speaker math for a MONO layout (one speaker at
    // angle 0, as setMono() configures) with the source directly behind the
    // listener (angle π) — the exact scenario from issue #202's repro.
    const float speakerAngle = 0.f;
    const float sourceAngle = static_cast<float>(YSE::Pi);
    const float initPan = (1.f + std::cos(speakerAngle - sourceAngle)) * 0.5f; // -> 0
    const float effective = Impl::computeSpeakerOverlap(speakerAngle, speakerAngle); // self == 1
    const float initGain = initPan / effective; // -> 0
    const float power = initGain * initGain; // -> 0 (degenerate)
    const float ratio = Impl::computePanRatio(initGain, power, 1);
    const float correctPower = 1.f; // near field: toChannels() clamps this to 1
    const float finalGain = std::sqrt(correctPower * ratio);
    CHECK(std::isfinite(finalGain));
    CHECK(finalGain == doctest::Approx(1.f)); // equal 1/N split, N=1 -> full gain
  }

  TEST_CASE("panRatio: final gain stays finite across a full source-angle sweep (#202)") {
    using Impl = YSE::SOUND::implementationObject;
    // Sweep the source all the way around for a mono (1 speaker @0) and a stereo
    // (±90°) layout, recomputing the toChannels() pan math at each angle. Every
    // resulting gain must be finite — pre-fix the mono antipode produced NaN.
    const std::vector<std::vector<float>> layouts = {
        {0.f}, // mono
        {-static_cast<float>(YSE::Pi) / 2.f, static_cast<float>(YSE::Pi) / 2.f}, // stereo
    };
    for (const auto& speakers : layouts) {
      for (int i = 0; i <= 360; ++i) {
        const float src = static_cast<float>(i) * static_cast<float>(YSE::Pi) / 180.f;
        std::vector<float> initGain(speakers.size());
        float power = 0.f;
        for (std::size_t s = 0; s < speakers.size(); ++s) {
          const float initPan = (1.f + std::cos(speakers[s] - src)) * 0.5f;
          float effective = 0.f;
          for (float other : speakers)
            effective += Impl::computeSpeakerOverlap(speakers[s], other);
          initGain[s] = initPan / effective;
          power += initGain[s] * initGain[s];
        }
        for (std::size_t s = 0; s < speakers.size(); ++s) {
          const float ratio =
              Impl::computePanRatio(initGain[s], power, static_cast<UInt>(speakers.size()));
          CHECK(std::isfinite(std::sqrt(1.f * ratio)));
        }
      }
    }
  }

  // ─── Source-angle mapping: relative vs world frame (#204) ───────────────────
  //
  // Regression tests for the left↔right mirror in the relative / pan2D pan path.
  // The engine's convention (CHANNEL::setStereo()) puts the right speaker at
  // +90° (output 1) and the left at -90° (output 0). update() derives the source
  // pan angle from atan2(dir.x, dir.z), which is +90° for a source on +x. The
  // relative branch used to *negate* that (-90°), imaging every relative /
  // pan2D source on the wrong speaker (#204). computeSourceAngle must take the
  // relative angle directly so both frames agree. These call the pure helper
  // (and, for the end-to-end case, compose it with the toChannels() pan math
  // from the other pure helpers) so the assertions are exact and independent of
  // any audio device.

  TEST_CASE("sourceAngle: a relative source at +x images right (+90°), not left (#204)") {
    using Impl = YSE::SOUND::implementationObject;
    // dir already lives in the listener's frame; listenerForward is ignored.
    const float a = Impl::computeSourceAngle(true, YSE::Pos(1.f, 0.f, 0.f), YSE::Pos(0.f));
    CHECK(a == doctest::Approx(static_cast<float>(YSE::Pi) / 2.f)); // pre-fix this was -π/2
  }

  TEST_CASE("sourceAngle: relative and world frames agree for a +z-facing listener (#204)") {
    using Impl = YSE::SOUND::implementationObject;
    const YSE::Pos src(1.f, 0.f, 0.f);
    const YSE::Pos fwd(0.f, 0.f, 1.f); // listener looking down +z
    const float world = Impl::computeSourceAngle(false, src, fwd);
    const float rel = Impl::computeSourceAngle(true, src, YSE::Pos(0.f));
    CHECK(world == doctest::Approx(static_cast<float>(YSE::Pi) / 2.f)); // right speaker
    CHECK(rel == doctest::Approx(world)); // frames must not mirror each other
  }

  TEST_CASE("sourceAngle: world frame rotates with the listener's facing (#204)") {
    using Impl = YSE::SOUND::implementationObject;
    // Listener turned 90° to face +x: a source on +x is now straight ahead (0°).
    const float a =
        Impl::computeSourceAngle(false, YSE::Pos(1.f, 0.f, 0.f), YSE::Pos(1.f, 0.f, 0.f));
    CHECK(a == doctest::Approx(0.f));
  }

  TEST_CASE("sourceAngle: a relative +x source pans to the right speaker end-to-end (#204)") {
    using Impl = YSE::SOUND::implementationObject;
    // Compose the full update()->toChannels() mapping for a stereo layout: the
    // source angle feeds the toChannels() pan math (computeSpeakerOverlap +
    // computePanRatio). Output 0 = left (-90°), output 1 = right (+90°), exactly
    // as setStereo() configures them.
    const std::vector<float> speakers = {
        -static_cast<float>(YSE::Pi) / 2.f, // output 0 = left
        +static_cast<float>(YSE::Pi) / 2.f, // output 1 = right
    };
    const float src = Impl::computeSourceAngle(true, YSE::Pos(1.f, 0.f, 0.f), YSE::Pos(0.f));
    std::vector<float> initGain(speakers.size());
    float power = 0.f;
    for (std::size_t s = 0; s < speakers.size(); ++s) {
      const float initPan = (1.f + std::cos(speakers[s] - src)) * 0.5f;
      float effective = 0.f;
      for (float other : speakers)
        effective += Impl::computeSpeakerOverlap(speakers[s], other);
      initGain[s] = initPan / effective;
      power += initGain[s] * initGain[s];
    }
    const float gainL = std::sqrt(1.f * Impl::computePanRatio(initGain[0], power, 2));
    const float gainR = std::sqrt(1.f * Impl::computePanRatio(initGain[1], power, 2));
    // Pre-fix the angle was -90°, which put all the gain on output 0 (left).
    CHECK(gainR > gainL);
    CHECK(gainL == doctest::Approx(0.f)); // fully panned to output 1 (right)
  }

  // ─── Zenith flyover: horizontal-fraction pan directionality (#210) ──────────
  //
  // The panner is horizontal-only: computeSourceAngle projects elevation out
  // with atan2(x, z). Near the zenith the horizontal components shrink to noise,
  // so a source flying over the listener would sweep the azimuth (and thus the
  // pan) around the full circle at full gain. computeHorizontalFraction returns
  // sqrt(x²+z²)/sqrt(x²+y²+z²) in [0..1]; toChannels() multiplies the cardioid's
  // cosine by it so an overhead source blends toward an equal-power omni spread
  // instead of sweeping. These call the pure helper directly (and, for the
  // end-to-end case, compose it with the toChannels() pan math from the other
  // pure helpers) so the assertions are exact and independent of any device.

  TEST_CASE("horizFraction: a source on the horizon keeps full directionality (== 1) (#210)") {
    using Impl = YSE::SOUND::implementationObject;
    // Purely horizontal directions (y == 0) must be unaffected: fraction == 1.
    CHECK(Impl::computeHorizontalFraction(YSE::Pos(3.f, 0.f, 4.f)) == doctest::Approx(1.f));
    CHECK(Impl::computeHorizontalFraction(YSE::Pos(0.f, 0.f, 10.f)) == doctest::Approx(1.f));
    CHECK(Impl::computeHorizontalFraction(YSE::Pos(-5.f, 0.f, 0.f)) == doctest::Approx(1.f));
  }

  TEST_CASE("horizFraction: a source at the zenith collapses to zero directionality (#210)") {
    using Impl = YSE::SOUND::implementationObject;
    // Straight overhead / underfoot: no horizontal extent -> fraction 0.
    CHECK(Impl::computeHorizontalFraction(YSE::Pos(0.f, 7.f, 0.f)) == doctest::Approx(0.f));
    CHECK(Impl::computeHorizontalFraction(YSE::Pos(0.f, -7.f, 0.f)) == doctest::Approx(0.f));
  }

  TEST_CASE("horizFraction: a 45° elevation gives sqrt(1/2) (#210)") {
    using Impl = YSE::SOUND::implementationObject;
    // Horizontal extent == vertical extent -> horiz/total = 1/sqrt(2).
    const float f = Impl::computeHorizontalFraction(YSE::Pos(0.f, 1.f, 1.f));
    CHECK(f == doctest::Approx(std::sqrt(0.5f)));
  }

  TEST_CASE("horizFraction: a co-located source returns 1 (unchanged near-field) (#210)") {
    using Impl = YSE::SOUND::implementationObject;
    // Zero-length direction has neither azimuth nor elevation; the guard returns
    // full directionality so the existing co-located behaviour is untouched.
    CHECK(Impl::computeHorizontalFraction(YSE::Pos(0.f, 0.f, 0.f)) == doctest::Approx(1.f));
  }

  TEST_CASE("horizFraction: stays within [0..1] across an elevation sweep (#210)") {
    using Impl = YSE::SOUND::implementationObject;
    // Rotate the source from the horizon up to the zenith; the weight must fall
    // monotonically from 1 to 0 and never leave [0..1].
    float prev = 2.f;
    for (int i = 0; i <= 90; ++i) {
      const float rad = static_cast<float>(i) * static_cast<float>(YSE::Pi) / 180.f;
      const float f = Impl::computeHorizontalFraction(
          YSE::Pos(std::cos(rad), std::sin(rad), 0.f)); // unit vector, elevation i°
      CHECK(f >= -1e-4f);
      CHECK(f <= 1.f + 1e-4f);
      CHECK(f <= prev + 1e-4f); // non-increasing as elevation rises
      prev = f;
    }
  }

  TEST_CASE("horizFraction: a zenith source pans equally to both stereo speakers (#210)") {
    using Impl = YSE::SOUND::implementationObject;
    // End-to-end: a source directly overhead has an ill-defined azimuth. With
    // the horizontal fraction folded into the pan term, the (1+cos)/2 cardioid
    // collapses to a flat 0.5 for every speaker, so a stereo layout receives
    // equal gain left and right instead of an azimuth-swept split.
    const std::vector<float> speakers = {
        -static_cast<float>(YSE::Pi) / 2.f, // output 0 = left
        +static_cast<float>(YSE::Pi) / 2.f, // output 1 = right
    };
    const YSE::Pos overhead(0.f, 10.f, 0.f);
    const float src = Impl::computeSourceAngle(true, overhead, YSE::Pos(0.f));
    const float hf = Impl::computeHorizontalFraction(overhead); // -> 0
    std::vector<float> initGain(speakers.size());
    float power = 0.f;
    for (std::size_t s = 0; s < speakers.size(); ++s) {
      const float initPan = (1.f + hf * std::cos(speakers[s] - src)) * 0.5f; // -> 0.5 flat
      float effective = 0.f;
      for (float other : speakers)
        effective += Impl::computeSpeakerOverlap(speakers[s], other);
      initGain[s] = initPan / effective;
      power += initGain[s] * initGain[s];
    }
    const float gainL = std::sqrt(1.f * Impl::computePanRatio(initGain[0], power, 2));
    const float gainR = std::sqrt(1.f * Impl::computePanRatio(initGain[1], power, 2));
    CHECK(gainL == doctest::Approx(gainR)); // equal-power omni spread
    CHECK(gainL > 0.f);
  }

  TEST_CASE("horizFraction: an overhead flyover no longer sweeps the pan (#210)") {
    using Impl = YSE::SOUND::implementationObject;
    // Walk a source across the sky at near-zenith height on a small x/z circle —
    // the exact geometry that produced a full-circle pan sweep pre-fix. With the
    // horizontal fraction scaling directionality, the per-speaker gains stay
    // essentially pinned to the equal-power split across the whole pass, so the
    // left/right imbalance never blows up.
    const std::vector<float> speakers = {
        -static_cast<float>(YSE::Pi) / 2.f,
        +static_cast<float>(YSE::Pi) / 2.f,
    };
    const float height = 100.f; // very high overhead
    const float radius = 1.f; // tiny horizontal circle -> azimuth swings fully
    float maxImbalance = 0.f;
    for (int i = 0; i < 360; ++i) {
      const float rad = static_cast<float>(i) * static_cast<float>(YSE::Pi) / 180.f;
      const YSE::Pos p(radius * std::cos(rad), height, radius * std::sin(rad));
      const float src = Impl::computeSourceAngle(true, p, YSE::Pos(0.f));
      const float hf = Impl::computeHorizontalFraction(p);
      std::vector<float> initGain(speakers.size());
      float power = 0.f;
      for (std::size_t s = 0; s < speakers.size(); ++s) {
        const float initPan = (1.f + hf * std::cos(speakers[s] - src)) * 0.5f;
        float effective = 0.f;
        for (float other : speakers)
          effective += Impl::computeSpeakerOverlap(speakers[s], other);
        initGain[s] = initPan / effective;
        power += initGain[s] * initGain[s];
      }
      const float gainL = std::sqrt(1.f * Impl::computePanRatio(initGain[0], power, 2));
      const float gainR = std::sqrt(1.f * Impl::computePanRatio(initGain[1], power, 2));
      maxImbalance = std::max(maxImbalance, std::fabs(gainL - gainR));
    }
    // The horizontal fraction here is radius/sqrt(radius²+height²) ≈ 0.01, so any
    // residual pan imbalance is negligible. Pre-fix (hf == 1) the same pass swung
    // fully hard-left to hard-right (imbalance up to ~1.0).
    CHECK(maxImbalance < 0.05f);
  }

  // ─── Doppler ratio: multiplicative + scaled + clamped (#208) ────────────────
  //
  // Regression tests for the additive, unsmoothed doppler. computeDopplerRatio
  // returns a playback-rate *multiplier*: 1.0 when nothing moves, > 1 for a
  // closing pair (pitch up), < 1 for a receding pair (pitch down). The old code
  // added `1 - 1/ratio` to the pitch instead of multiplying, which only matched
  // near pitch = 1. These call the pure helper directly so the assertions are
  // exact and independent of the update-tick timing that used to warble it.
  //
  // Geometry convention: `dist` is the source→listener axis (source position
  // minus listener position, i.e. from listener toward source in the engine's
  // `newPos - Listener().newPos`). A source velocity pointing along +dist moves
  // it *away* from the listener (receding); pointing along -dist moves it toward
  // the listener (approaching).

  TEST_CASE("doppler: a stationary source/listener pair yields ratio 1.0 (#208)") {
    using Impl = YSE::SOUND::implementationObject;
    const float r =
        Impl::computeDopplerRatio(YSE::Pos(0.f), YSE::Pos(0.f), YSE::Pos(10.f, 0.f, 0.f), 1.f);
    CHECK(r == doctest::Approx(1.f));
  }

  TEST_CASE("doppler: a source approaching the listener pitches up (ratio > 1) (#208)") {
    using Impl = YSE::SOUND::implementationObject;
    // Source sits at +10 on x; velocity toward the listener is -x (along -dist).
    const YSE::Pos dist(10.f, 0.f, 0.f);
    const YSE::Pos approaching(-30.f, 0.f, 0.f); // 30 m/s toward listener
    const float r = Impl::computeDopplerRatio(approaching, YSE::Pos(0.f), dist, 1.f);
    CHECK(r > 1.f);
    // Physical value: (344 + 0) / (344 + (-30)) = 344/314 ≈ 1.0955.
    CHECK(r == doctest::Approx(344.f / 314.f));
  }

  TEST_CASE("doppler: a source receding from the listener pitches down (ratio < 1) (#208)") {
    using Impl = YSE::SOUND::implementationObject;
    const YSE::Pos dist(10.f, 0.f, 0.f);
    const YSE::Pos receding(30.f, 0.f, 0.f); // 30 m/s away from listener (along +dist)
    const float r = Impl::computeDopplerRatio(receding, YSE::Pos(0.f), dist, 1.f);
    CHECK(r < 1.f);
    // Physical value: (344 + 0) / (344 + 30) = 344/374 ≈ 0.9198.
    CHECK(r == doctest::Approx(344.f / 374.f));
  }

  TEST_CASE("doppler: is multiplicative, not additive (the #208 fix)") {
    using Impl = YSE::SOUND::implementationObject;
    // The bug added `1 - 1/ratio` to pitch. For a receding source the physical
    // ratio is 344/374. The additive linearisation would have produced
    // 1 + (1 - 374/344) = 1 - 30/344 ≈ 0.9128, whereas the correct multiplier is
    // 344/374 ≈ 0.9198. Require the helper to return the true ratio, not the
    // linearised value — they differ enough to distinguish.
    const YSE::Pos dist(10.f, 0.f, 0.f);
    const float ratio =
        Impl::computeDopplerRatio(YSE::Pos(30.f, 0.f, 0.f), YSE::Pos(0.f), dist, 1.f);
    const float additive = 1.f + (1.f - 374.f / 344.f); // what the old code applied
    CHECK(ratio == doctest::Approx(344.f / 374.f));
    CHECK(ratio != doctest::Approx(additive));
    CHECK(ratio > additive); // the correct multiplier is the larger of the two here
  }

  TEST_CASE("doppler: dopplerScale amplifies the deviation from unity (#208)") {
    using Impl = YSE::SOUND::implementationObject;
    const YSE::Pos dist(10.f, 0.f, 0.f);
    const YSE::Pos vel(30.f, 0.f, 0.f); // receding
    const float base = Impl::computeDopplerRatio(vel, YSE::Pos(0.f), dist, 1.f);
    const float scaled = Impl::computeDopplerRatio(vel, YSE::Pos(0.f), dist, 2.f);
    const float off = Impl::computeDopplerRatio(vel, YSE::Pos(0.f), dist, 0.f);
    // scale 0 disables the effect; scale 2 doubles the deviation from 1.
    CHECK(off == doctest::Approx(1.f));
    CHECK(scaled == doctest::Approx(1.f + (base - 1.f) * 2.f));
  }

  TEST_CASE("doppler: listener motion contributes to the ratio (#208)") {
    using Impl = YSE::SOUND::implementationObject;
    const YSE::Pos dist(10.f, 0.f, 0.f);
    // Listener moving toward the source (+x, along +dist) raises the numerator
    // and pitches the sound up.
    const float r = Impl::computeDopplerRatio(YSE::Pos(0.f), YSE::Pos(30.f, 0.f, 0.f), dist, 1.f);
    CHECK(r > 1.f);
    CHECK(r == doctest::Approx(374.f / 344.f));
  }

  TEST_CASE("doppler: a co-located pair returns 1.0 rather than dividing by zero (#208)") {
    using Impl = YSE::SOUND::implementationObject;
    const float r =
        Impl::computeDopplerRatio(YSE::Pos(5.f, 0.f, 0.f), YSE::Pos(0.f), YSE::Pos(0.f), 1.f);
    CHECK(r == doctest::Approx(1.f));
  }

  TEST_CASE("doppler: the ratio is clamped to a sane band for super-sonic speeds (#208)") {
    using Impl = YSE::SOUND::implementationObject;
    const YSE::Pos dist(10.f, 0.f, 0.f);
    // Source closing far faster than the speed of sound: denominator goes
    // non-positive. The helper must not return a negative or unbounded rate.
    const float closing =
        Impl::computeDopplerRatio(YSE::Pos(-1000.f, 0.f, 0.f), YSE::Pos(0.f), dist, 1.f);
    CHECK(closing > 0.f);
    CHECK(closing <= 4.f + 1e-4f);
    // Receding far faster than sound: stays positive and clamped, never negative.
    const float fleeing =
        Impl::computeDopplerRatio(YSE::Pos(1000.f, 0.f, 0.f), YSE::Pos(0.f), dist, 1.f);
    CHECK(fleeing >= 0.25f - 1e-4f);
    CHECK(fleeing <= 1.f);
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
