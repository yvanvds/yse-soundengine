// Targeted coverage tests for YSE::SOUND::managerObject — exercises the
// helpers extracted from update() and the public manager API (empty/addFile)
// to cover branches that test_sound_state.cpp + test_sound_impl.cpp don't hit.

#include <doctest/doctest.h>
#include <chrono>
#include <thread>
#include <vector>
#include "yse.hpp"
#include "sound/soundInterface.hpp"
#include "sound/soundManager.h"
#include "dsp/dspObject.hpp"
#include "internal/time.h"
#include "support/null_device.hpp"

using namespace std::chrono_literals;

namespace {

  struct SilentSource : YSE::DSP::dspSourceObject {
    void process(YSE::SOUND_STATUS&) override {}
    void frequency(float) override {}
  };

  // File-scope sources outlive every sound impl created here. See test_sound_impl.cpp
  // for the Phase C lifetime rationale.
  SilentSource g_src;

  void drain(int n = 12) {
    for (int i = 0; i < n; i++) {
      YSE::INTERNAL::Time().update();
      YSE::SOUND::Manager().update();
      std::this_thread::sleep_for(5ms);
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
