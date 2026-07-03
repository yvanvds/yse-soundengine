// Regression tests for issue #186: the soundFiles list was raced between the
// main thread (addFile) and the audio-thread GC in Manager::update(), and
// ~soundFile (sf_close + delete[]) ran on the audio callback.
//
// The fix moves soundFile garbage collection off the audio thread onto a
// slow-pool job (mgrFileGC) that owns the list structure together with addFile
// under soundFilesMutex, and guards the per-file clientList with its own mutex.
// update() no longer touches soundFiles at all.
//
// Like the other lifecycle races in this suite (issue #41), the assertion is
// crash-freedom under concurrent traffic; the value is realised under the
// ThreadSanitizer CI build, which flags the unsynchronised soundFiles / clientList
// crossings on the pre-fix code and stays clean on the fixed code.

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include "yse.hpp"
#include "sound/soundInterface.hpp"
#include "sound/soundManager.h"
#include "dsp/buffer.hpp"
#include "internal/time.h"
#include "support/null_device.hpp"

namespace {

  // File-scope buffers used as sound sources. addFile() dedups by buffer pointer,
  // so a pool of distinct buffers yields distinct soundFiles for the GC pass to
  // walk while addFile is concurrently emplacing. They must outlive every soundFile
  // that references them.
  std::vector<YSE::DSP::buffer>& sourceBuffers() {
    static std::vector<YSE::DSP::buffer> bufs = [] {
      std::vector<YSE::DSP::buffer> v;
      v.reserve(32);
      for (int i = 0; i < 32; ++i)
        v.emplace_back(128);
      return v;
    }();
    return bufs;
  }

  // Drive the manager forward: this is the audio-thread surrogate in the paused
  // engine. Post-fix it schedules the slow-pool GC job; pre-fix it ran the GC
  // inline and raced any concurrent addFile.
  void drain(int iterations, int sleepMs = 1) {
    for (int i = 0; i < iterations; ++i) {
      YSE::INTERNAL::Time().update();
      YSE::SOUND::Manager().update();
      if (sleepMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
  }

} // namespace

TEST_SUITE("sound") {

  // ─── The exact crossing: main-thread addFile vs the file GC ──────────────────
  //
  // A worker thread hammers Manager().addFile() (the operation that raced) while
  // the test thread drives update(). Pre-fix, update()'s inline GC iterated
  // soundFiles unsynchronised against the worker's emplace_front — a data race and
  // potential UAF. Post-fix, addFile and the slow-pool GC serialise on
  // soundFilesMutex and update() never touches the list.
  TEST_CASE("SOUND #186: concurrent addFile and file GC do not race") {
    if (!TestHelpers::engineInit()) return;

    auto& bufs = sourceBuffers();
    std::atomic<bool> workerDone{false};
    constexpr int ROUNDS = 400;

    std::thread worker([&]() {
      for (int r = 0; r < ROUNDS; ++r) {
        // Walk the whole pool so addFile both finds existing entries and
        // emplaces new ones, exercising the list traversal under the lock.
        YSE::INTERNAL::soundFile* sf = YSE::SOUND::Manager().addFile(&bufs[r % bufs.size()]);
        (void)sf;
        std::this_thread::sleep_for(std::chrono::microseconds(20));
      }
      workerDone.store(true, std::memory_order_release);
    });

    // Keep updating (scheduling GC) until the worker is done. The loop runs long
    // enough that fileGCTimer crosses its ~1s threshold and the slow-pool GC job
    // actually fires and iterates soundFiles concurrently with the worker.
    int safety = 20000;
    while (!workerDone.load(std::memory_order_acquire) && --safety > 0) {
      drain(2, 0);
    }
    worker.join();
    drain(20); // settle: let any in-flight GC job finish

    CHECK(true); // crash-freedom (and TSan-cleanliness) is the assertion
  }

  // ─── End-to-end churn with real clients: attach / release / inUse crossing ───
  //
  // Buffer-backed sounds populate soundFiles AND drive attach() (create, main
  // thread) + release() (~implementationObject, slow pool), while the GC's inUse()
  // reads clientList. Creating/destroying them under the live audio callback puts
  // the real audio thread on update() (scheduling GC) at the same time.
  TEST_CASE("SOUND #186: buffer-backed sound churn under live audio callback" *
            doctest::test_suite("integration")) {
    if (!TestHelpers::engineInitWithAudio()) return;

    auto& bufs = sourceBuffers();
    constexpr int N = 150;
    for (int i = 0; i < N; ++i) {
      YSE::sound s;
      s.create(bufs[i % bufs.size()]);
      s.play();
      // Short lifetime: the impl attaches to its soundFile and is released
      // almost immediately, so attach/release churn overlaps the audio
      // callback's GC scheduling.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } // ~sound() releases each impl; the slow pool runs release() + GC.

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(true);
  }

} // TEST_SUITE("sound")
