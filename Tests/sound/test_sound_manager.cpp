// Targeted coverage tests for YSE::SOUND::managerObject — exercises the
// helpers extracted from update() and the public manager API (empty/addFile)
// to cover branches that test_sound_state.cpp + test_sound_impl.cpp don't hit.

#include <doctest/doctest.h>
#include <chrono>
#include <cstddef>
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
      drain(); // let the impl reach inUse
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
    // Assert the invariant rather than an absolute empty() value: the SOUND
    // manager is a process-wide singleton other tests have touched, so the
    // baseline may already be non-empty. `after == before` holds on the fixed
    // code regardless; on the pre-fix code it breaks whenever the baseline is
    // empty (true -> false), so the test can only ever fail on a regression.
    drain(); // settle any pending lifecycle work first
    const bool before = YSE::SOUND::Manager().empty();
    {
      YSE::sound s;
      s.create("/no/such/file.wav"); // FileExists() fails -> create() returns false
      CHECK(s.isValid() == false); // confirm we hit the failure path
    }
    drain();
    const bool after = YSE::SOUND::Manager().empty();
    CHECK(after == before);
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

    drain(24); // let the audio-thread surrogate schedule the slow-pool delete job
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

    drain(24);
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

} // TEST_SUITE("sound")
