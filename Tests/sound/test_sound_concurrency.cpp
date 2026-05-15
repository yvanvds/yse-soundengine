// Concurrency stress tests for SOUND::Manager (Phase D of the
// cross-thread fix tracked in issue #41).
//
// These tests exercise the three races that Phase A (mutex on
// `implementations` + lock-free inbox for `toLoad`), Phase B (audio-thread
// disconnect-before-delete ordering), and Phase C (atomic `source_dsp`)
// are meant to close:
//
//   1. main vs slow-pool on `implementations` (addImplementation +
//      setupJob iteration + deleteJob remove_if).
//   2. main vs audio-thread on `toLoad` (push to inbox + audio-thread
//      drain into toLoad).
//   3. audio-thread vs slow-pool on `parent->sounds` via destructor-
//      driven disconnect.
//
// The first two cases ("paused" tests) run under the standard unit-test
// engine init (audio stream paused — see Tests/support/null_device.hpp);
// the test thread drives Manager().update() and acts as the "audio
// thread" for synchronisation purposes. The third case
// ("integration" tag) opens the real audio device via
// engineInitWithAudio() so the PortAudio callback fires concurrently.
//
// Sound counts are kept modest (a few hundred) so the suite finishes in
// well under a second even under churn. The point isn't volume, it's to
// guarantee the create→destroy lifecycle survives concurrent main-side
// and audio-side traffic without the heap-corruption / pure-virtual
// crashes tracked in issue #41.

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "yse.hpp"
#include "sound/soundInterface.hpp"
#include "sound/soundManager.h"
#include "dsp/dspObject.hpp"
#include "internal/time.h"
#include "support/null_device.hpp"

namespace {

// File-scope DSP source — must outlive every sound created here. See the
// matching comment in test_sound_impl.cpp; Phase C's atomic source_dsp +
// release-time nullification provides defence-in-depth, but the contract is
// still that the caller owns lifetime.
struct ConcurrencySilentSource : YSE::DSP::dspSourceObject {
    void process(YSE::SOUND_STATUS&) override {}
    void frequency(float) override {}
};

ConcurrencySilentSource g_concSrc;

// Drive SOUND::Manager().update() repeatedly with brief sleeps so the
// slow-pool has time to claim impls (setupJob) and free released ones
// (deleteJob). Each iteration also advances INTERNAL::Time() so any
// time-based machinery downstream behaves normally.
void drain(int iterations = 8, int sleepMs = 2) {
    for (int i = 0; i < iterations; ++i) {
        YSE::INTERNAL::Time().update();
        YSE::SOUND::Manager().update();
        if (sleepMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
}

} // namespace

TEST_SUITE("sound") {

// ─── Single-thread churn: many create→destroy cycles on one thread ───────────

TEST_CASE("sound concurrency: single-thread create/destroy churn does not crash") {
    if (!TestHelpers::engineInit()) return;

    constexpr int N = 200;
    for (int i = 0; i < N; ++i) {
        YSE::sound s;
        s.create(g_concSrc);
        s.play();
        // Drain a couple of times between iterations so the slow-pool can
        // make forward progress instead of accumulating impls.
        if ((i & 0x0f) == 0) drain(2);
    } // ~sound at end of each iteration releases impl through OBJECT_RELEASE.

    // Final settle: enough iterations to let the slow-pool free everything.
    drain(40);
    CHECK(true); // crash-freedom is the assertion
}

// ─── Two-thread churn: worker thread creates/destroys while test thread updates

TEST_CASE("sound concurrency: two-thread create/destroy churn does not crash") {
    if (!TestHelpers::engineInit()) return;

    std::atomic<bool> workerDone{false};
    constexpr int N = 100;

    std::thread worker([&]() {
        for (int i = 0; i < N; ++i) {
            YSE::sound s;
            s.create(g_concSrc);
            s.play();
            // Mild yield so the test thread can interleave update() ticks
            // — the point is to overlap addImplementation / setup with the
            // audio-thread drain and the slow-pool's iteration of
            // `implementations`.
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        workerDone.store(true, std::memory_order_release);
    });

    // Drive the audio-thread surrogate (update + setup/delete enqueues) for
    // as long as the worker is still pushing sounds in. Cap the loop so a
    // stuck worker can't hang the test.
    int safety = 5000;
    while (!workerDone.load(std::memory_order_acquire) && --safety > 0) {
        drain(2, 0); // no sleep here — the worker thread has its own pacing
    }
    worker.join();

    // Final settle for the slow-pool to free anything still pending.
    drain(40);
    CHECK(true);
}

// ─── Sustained churn: keep creating while the slow-pool is still freeing ─────

TEST_CASE("sound concurrency: overlapping create/destroy keeps lifecycle coherent") {
    if (!TestHelpers::engineInit()) return;

    // Run several "waves" of N sounds. Between waves the test thread does a
    // small number of drains, then immediately starts the next wave —
    // deliberately preventing the slow-pool from quiescing before more work
    // arrives. This exercises the case where toLoad contains impls in
    // OBJECT_CREATED, OBJECT_SETTING_UP, OBJECT_SETUP, and OBJECT_READY
    // simultaneously while addImplementation fires from the main thread.
    constexpr int WAVES = 5;
    constexpr int N = 60;
    for (int w = 0; w < WAVES; ++w) {
        for (int i = 0; i < N; ++i) {
            YSE::sound s;
            s.create(g_concSrc);
            s.play();
        }
        drain(4); // partial drain — leaves work for the next wave
    }
    drain(60); // final settle
    CHECK(true);
}

// ─── Live audio variant — tagged [integration], skipped in normal unit run ───
//
// Phases A+B+C are designed to make this safe on a real audio device. The
// engine init helper resumes the PortAudio stream, so update() and dsp()
// run on the actual audio thread. Concurrent create/destroy from the test
// thread now races against a live audio callback that iterates
// parent->sounds.

TEST_CASE("sound concurrency: create/destroy churn under live audio callback"
          * doctest::test_suite("integration")) {
    if (!TestHelpers::engineInitWithAudio()) return;

    constexpr int N = 100;
    for (int i = 0; i < N; ++i) {
        YSE::sound s;
        s.create(g_concSrc);
        s.play();
        // Brief lifetime so the impl gets connected and then immediately
        // released — exercises the audio-thread disconnect-before-delete
        // ordering of Phase B repeatedly. The microsleep is small but
        // non-zero so the audio callback is likely to fire at least once
        // while the sound is live.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // The live audio callback drives update() itself; just give it time.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(true);
}

} // TEST_SUITE("sound")
