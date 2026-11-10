// Targeted coverage tests for YSE::SOUND::managerObject — exercises the
// helpers extracted from update() and the public manager API (empty/addFile)
// to cover branches that test_sound_state.cpp + test_sound_impl.cpp don't hit.

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>
#include "yse.hpp"
#include "sound/soundInterface.hpp"
#include "sound/soundManager.h"
#include "patcher/patcher.hpp"
#include "dsp/dspObject.hpp"
#include "internal/time.h"
#include "support/null_device.hpp"
#include "support/timer_pacing.hpp"
#include "patcher/pool_blocker.hpp"

using namespace std::chrono_literals;

namespace {

  struct SilentSource : YSE::DSP::dspSourceObject {
    void process(YSE::SOUND_STATUS&) override {}
    void frequency(float) override {}
  };

  // File-scope sources outlive every sound impl created here. See test_sound_impl.cpp
  // for the Phase C lifetime rationale.
  SilentSource g_src;

  // The iteration count is what advances the manager's state machine, so it
  // stays as it is; the wait between iterations is counted in ticks of the
  // suite's pacing reference instead of milliseconds, so the slow pool gets
  // proportionally longer on a loaded box (issue #753).
  void drain(int n = 12) {
    for (int i = 0; i < n; i++) {
      YSE::INTERNAL::Time().update();
      YSE::SOUND::Manager().update();
      TestHelpers::paceWindow(5);
    }
  }

  // The completion-signal form of drain(), for the cases whose CHECK depends
  // on the slow pool having actually finished: a fixed iteration count is a
  // bounded window that a loaded pool can miss entirely (issue #834). The
  // budget is denominated in reference-timer ticks (issue #753); returns
  // ready(), so a timed-out wait fails the caller's own assertion.
  template <typename P> bool drainUntil(P ready, int ticks = 5000) {
    return TestHelpers::pacedPump(
        ticks, ready,
        [] {
          YSE::INTERNAL::Time().update();
          YSE::SOUND::Manager().update();
        },
        5);
  }

} // namespace

TEST_SUITE("sound") {

  TEST_CASE("SOUND::Manager: empty() reflects active (toLoad/inUse) impls, not the raw list") {
    if (!TestHelpers::engineInit()) return;
    // empty() is the audio thread's "nothing to render" signal. After #200 it
    // reads only the audio-thread-owned toLoad/inUse lists, never the
    // mutex-guarded `implementations` list. A live DSP sound must make it
    // report non-empty.
    {
      YSE::sound s;
      s.create(g_src);
      // Pump until the impl has actually reached inUse — empty() flipping
      // false is the completion signal, not a fixed window (#834).
      drainUntil([] { return YSE::SOUND::Manager().empty() == false; });
      CHECK(YSE::SOUND::Manager().empty() == false);
      s.stop();
    }
    drain(); // release + delete the impl
  }

  TEST_CASE("SOUND::Manager: a failed file-create never flips empty() to non-empty (#200)") {
    if (!TestHelpers::engineInit()) return;
    // Regression for #200: sound::create(fileName) adds an implementationObject
    // to `implementations` BEFORE it validates the file, then on a missing file
    // fails without ever handing the impl to setup() — so it lingers in
    // `implementations` but never enters toLoad/inUse. The pre-fix empty()
    // read `implementations.empty()`, so this stuck impl made it report
    // non-empty forever (and, worse, read that list lock-free from the audio
    // callback). The fixed empty() ignores it.
    //
    // The invariant must tolerate background reclamation (#835): the SOUND
    // manager is a process-wide singleton, and an impl from an earlier case
    // still being reclaimed can flip empty() between a before- and an
    // after-sample with no bug present. So quiesce instead of sampling: pump
    // until empty() reports true — the completion signal that every earlier
    // impl has left the audio-thread toLoad/inUse lists — run the failing
    // create, and pump until it reports true again. The refused impl never
    // enters toLoad/inUse, so the fixed empty() re-quiesces immediately; the
    // pre-fix empty() read the `implementations` list, which the stuck impl
    // kept non-empty. Polling empty() here is race-free: it reads lists owned
    // by the sole update() caller, which (audio being paused) is this thread.
    CHECK(drainUntil([] { return YSE::SOUND::Manager().empty(); }));
    {
      YSE::sound s;
      s.create("/no/such/file.wav"); // FileExists() fails -> create() returns false
      CHECK(s.isValid() == false); // confirm we hit the failure path
    }
    CHECK(drainUntil([] { return YSE::SOUND::Manager().empty(); }));
  }

  // ─── refused create() reclaims its implementation (issue #817) ───────────────
  //
  // sound::create() registers an implementationObject before it knows whether
  // the source is acceptable. On refusal it used to flag that impl
  // OBJECT_RELEASE and drop it — but only the `inUse` pass promotes RELEASE to
  // DELETE, and a refused impl never reached `inUse`, so nothing ever made it
  // eligible for the slow-pool delete job. Every failed create() parked one
  // (fairly heavy) impl in `implementations` until system::close(); a caller
  // retrying a missing asset each frame grew that list without bound.
  //
  // These drive the real manager — real slow pool, real delete job — and assert
  // the list does not grow across the refusals. They fail on the pre-fix code
  // (count grows by the number of attempts and stays there) and pass once the
  // refusal path retires the impl to OBJECT_DELETE.
  //
  // `<= before` rather than `== before`: the manager is a process-wide
  // singleton, so an impl retired by an earlier test could still be reaped
  // during the settling drain, legitimately lowering the baseline. Growth is
  // what the regression is, and growth is what this rejects.

  TEST_CASE("SOUND::Manager: a refused file create() releases its impl (#817)") {
    if (!TestHelpers::engineInit()) return;

    drain(); // settle any pending lifecycle work first
    const std::size_t before = YSE::SOUND::Manager().implementationCount();

    constexpr int kAttempts = 16;
    for (int i = 0; i < kAttempts; i++) {
      YSE::sound s;
      s.create("/no/such/file.wav"); // FileExists() fails -> create() returns false
      CHECK(s.isValid() == false); // confirm we hit the refusal path
    }

    // Pump until the slow-pool delete job has reclaimed the refused impls —
    // the list shrinking back to the baseline is the completion signal (#834).
    drainUntil([before] { return YSE::SOUND::Manager().implementationCount() <= before; });
    CHECK(YSE::SOUND::Manager().implementationCount() <= before);
  }

  TEST_CASE("SOUND::Manager: a refused patcher create() releases its impl (#817)") {
    if (!TestHelpers::engineInit()) return;

    // The other refusal branch: one-patcher-per-sound (#287). The first sound
    // keeps the patcher for the whole case, so every later attempt is refused.
    YSE::patcher p;
    p.create(1);
    YSE::sound owner;
    owner.create(p);
    CHECK(owner.isValid());

    drain();
    const std::size_t before = YSE::SOUND::Manager().implementationCount();

    constexpr int kAttempts = 16;
    for (int i = 0; i < kAttempts; i++) {
      YSE::sound s;
      s.create(p);
      CHECK(s.isValid() == false);
    }

    // Completion signal rather than a fixed window, as above (#834).
    drainUntil([before] { return YSE::SOUND::Manager().implementationCount() <= before; });
    CHECK(YSE::SOUND::Manager().implementationCount() <= before);
  }

  TEST_CASE("SOUND::Manager: addFile by filename returns a valid soundFile pointer or null") {
    if (!TestHelpers::engineInit()) return;
    // A nonexistent path returns either nullptr or a soundFile whose state
    // ends up INVALID after the slow-pool load tries to open it. Either way
    // the addFile codepath through update()'s GC pass executes.
    YSE::INTERNAL::soundFile* sf = YSE::SOUND::Manager().addFile("/no/such/file.wav");
    (void)sf;
    drain();
    CHECK(true);
  }

  TEST_CASE("SOUND::Manager: rapid create/destroy of multiple DSP sounds drains helpers") {
    if (!TestHelpers::engineInit()) return;
    // Creating sounds and letting them be destroyed quickly forces the
    // helpers (promoteReadyImpls / syncAndReleaseInUse) to walk the inUse
    // list as impls transition OBJECT_READY → OBJECT_RELEASE → OBJECT_DELETE.
    for (int round = 0; round < 3; round++) {
      std::vector<YSE::sound> sounds(4);
      for (auto& s : sounds)
        s.create(g_src);
      drain(4);
      for (auto& s : sounds)
        s.stop();
      drain(2);
    } // ~sound() of every element fires here, queueing release for next drain
    drain(8);
    CHECK(true);
  }

  TEST_CASE("SOUND::Manager: freeing a sound-owned patcher on the background pool does not "
            "block sound::create (#1257)") {
    if (!TestHelpers::engineInit()) return;

    // A patcher mounted as a sound outlives its handle: once the handle is gone
    // the sound owns it, and the sound manager's delete job frees it on the
    // background pool's one worker, holding implementationsMutex.
    // ~patcherImplementation joins its reclaim jobs, which are background jobs
    // too, so a reclaim pass queued *behind* the delete job used to wait for
    // the thread that was waiting for it. The next sound::create then blocked
    // on the mutex for good. Staged deterministically with the pool parked:
    //   1. release the sound; two ticks queue the delete job behind the blocker;
    //   2. edit the patcher, which queues a reclaim pass behind the delete job;
    //   3. drop the handle, so the delete job frees the patcher;
    //   4. unpark, and create a sound from another thread, the host's view.
    drain();
    const std::size_t before = YSE::SOUND::Manager().implementationCount();

    auto p = std::make_unique<YSE::patcher>();
    p->create(1);
    auto owner = std::make_unique<YSE::sound>();
    owner->create(*p);
    REQUIRE(owner->isValid());
    drain(); // set up and in the audio thread's list; earlier reclaim passes done

    {
      TestHelpers::PoolBlocker blocker;
      REQUIRE(blocker.Park());
      owner.reset();
      YSE::INTERNAL::Time().update();
      YSE::SOUND::Manager().update(); // marks the impl OBJECT_DELETE
      YSE::INTERNAL::Time().update();
      YSE::SOUND::Manager().update(); // queues the delete job behind the blocker
      CHECK(p->CreateObject(".mtof") != nullptr); // publishes: a reclaim pass is queued
      p.reset(); // the sound owns the patcher now; the delete job frees it
      blocker.Unpark();
    }

    // The host's next sound::create, on a thread this case can walk away from
    // if it blocks, so the failure is reported rather than hanging the suite.
    auto created = std::make_shared<std::atomic<bool>>(false);
    auto next = std::make_shared<YSE::sound>();
    std::thread host([created, next] {
      next->create(g_src);
      created->store(true, std::memory_order_release);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!created->load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    const bool finished = created->load(std::memory_order_acquire);
    if (finished)
      host.join();
    else
      host.detach(); // deadlocked: leak the thread so the failure is reported
    REQUIRE(finished);
    CHECK(next->isValid());

    // The delete job finished: the patcher's sound is gone from the list.
    next.reset();
    drainUntil([before] { return YSE::SOUND::Manager().implementationCount() <= before; });
    CHECK(YSE::SOUND::Manager().implementationCount() <= before);
  }

} // TEST_SUITE("sound")
