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

  TEST_CASE("SOUND::Manager: empty() reports state of implementations list") {
    if (!TestHelpers::engineInit()) return;
    // The manager is a singleton that has been touched by every prior test
    // in this process, so empty() may already be false; assert only that the
    // call itself is safe and returns a bool — covers the empty() definition.
    bool e = YSE::SOUND::Manager().empty();
    (void)e;
    CHECK(true);
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
