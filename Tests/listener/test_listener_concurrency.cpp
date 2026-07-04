// Regression + stress tests for issue #196: the listener's spatial vectors
// (pos / forward / up / vel) used to be three independent std::atomic<Flt>, so
// a component-wise read on the audio thread could observe a mixed-generation
// vector (a fresh x combined with a stale y/z) while the control thread wrote
// component-wise. They are now seqlock-guarded (YseEngine/utils/atomicPos.h),
// which publishes and reads all three components as one consistent snapshot.
//
// Each test encodes a coherence invariant that only a tear-free snapshot can
// satisfy, then hammers the writer/reader race. On the old
// three-independent-atomics design a reader eventually captures a torn triple
// (verified by temporarily stripping the sequence guard); with the seqlock the
// invariant holds for every observed snapshot.

#include <doctest/doctest.h>
#include <atomic>
#include <thread>
#include "listener.hpp"
#include "implementations/listenerImplementation.h"
#include "utils/atomicPos.h"
#include "utils/vector.hpp"
#include "internal/settings.h"
#include "internal/time.h"
#include "support/null_device.hpp"

TEST_SUITE("listener") {

  // ─── aPos primitive: the seqlock itself ─────────────────────────────────────

  TEST_CASE("listener concurrency: aPos never yields a torn snapshot under a writer/reader race") {
    YSE::aPos p;
    // Seed a value that already satisfies the invariant, so the reader never
    // observes the default-constructed (0,0,0) (which is coherent but off the
    // (v, v+1000, v+2000) signature the writer publishes).
    p.store(0.f, 1000.f, 2000.f);

    std::atomic<bool> stop{false};
    std::atomic<bool> torn{false};
    long long reads = 0;

    // Writer publishes coherent triples (k, k+1000, k+2000) with k in [0,1000)
    // so every component is an exactly representable float and adjacent
    // generations always differ — any tear mixes two generations and breaks the
    // y == x+1000 / z == x+2000 relation.
    std::thread writer([&]() {
      int k = 0;
      while (!stop.load(std::memory_order_acquire)) {
        const float v = static_cast<float>(k % 1000);
        p.store(v, v + 1000.f, v + 2000.f);
        ++k;
      }
    });

    for (int i = 0; i < 3'000'000 && !torn.load(std::memory_order_relaxed); ++i) {
      const YSE::Pos s = p.load();
      if (s.y != s.x + 1000.f || s.z != s.x + 2000.f) {
        torn.store(true, std::memory_order_relaxed);
      }
      ++reads;
    }

    stop.store(true, std::memory_order_release);
    writer.join();

    CHECK_FALSE(torn.load(std::memory_order_relaxed));
    CHECK(reads > 0);
  }

  // ─── Integration: real audio update path reads the control-thread position ──

  TEST_CASE("listener concurrency: audio-path position stays coherent while the control thread "
            "moves the listener") {
    if (!TestHelpers::engineInit()) return;
    YSE::INTERNAL::Settings().distanceFactor = 1.f;

    std::atomic<bool> stop{false};
    std::atomic<bool> torn{false};

    // Seed a coherent (k,k,k) baseline so the reader never inspects a leftover
    // position from an earlier test (e.g. (1,2,4)) that legitimately fails the
    // x == y == z invariant this test relies on.
    YSE::Listener().pos(YSE::Pos(0.f, 0.f, 0.f));

    // Control thread: moves the listener to coherent positions (k,k,k) via the
    // public setter, exactly as an application would.
    std::thread mover([&]() {
      int k = 0;
      while (!stop.load(std::memory_order_acquire)) {
        const float v = static_cast<float>(k % 1000);
        YSE::Listener().pos(YSE::Pos(v, v, v));
        ++k;
      }
    });

    // Audio-thread stand-in: drive the real listenerImplementation::update()
    // (which reads the position snapshot) and inspect the scaled result the
    // audio path consumes. With distanceFactor == 1 a coherent source position
    // yields x == y == z; the old component-wise read could tear it here.
    long long ticks = 0;
    for (int i = 0; i < 300'000 && !torn.load(std::memory_order_relaxed); ++i) {
      YSE::INTERNAL::Time().update();
      YSE::INTERNAL::ListenerImpl().update();
      const YSE::Pos& scaled = YSE::INTERNAL::ListenerImpl().getPos();
      if (scaled.x != scaled.y || scaled.y != scaled.z) {
        torn.store(true, std::memory_order_relaxed);
      }
      ++ticks;
    }

    stop.store(true, std::memory_order_release);
    mover.join();

    CHECK_FALSE(torn.load(std::memory_order_relaxed));
    CHECK(ticks > 0);

    // Restore the clean baseline other listener tests assume.
    YSE::Listener().pos(YSE::Pos(0.f, 0.f, 0.f));
  }

  // ─── Integration: orientation reads race orient() through the public API ────

  TEST_CASE(
      "listener concurrency: orientation reads stay coherent while orient() runs concurrently") {
    if (!TestHelpers::engineInit()) return;

    std::atomic<bool> stop{false};
    std::atomic<bool> torn{false};

    // Seed both vectors on their invariant signatures so the reader never
    // catches the pre-race baseline (forward zero / up +Y) as a false tear.
    YSE::Listener().orient(YSE::Pos(0.f, 1000.f, 2000.f), YSE::Pos(0.f, 0.f, 0.f));

    // Control thread rewrites both orientation vectors. forward carries the
    // (k, k+1000, k+2000) signature; up carries the (k,k,k) signature.
    std::thread orienter([&]() {
      int k = 0;
      while (!stop.load(std::memory_order_acquire)) {
        const float v = static_cast<float>(k % 1000);
        YSE::Listener().orient(YSE::Pos(v, v + 1000.f, v + 2000.f), YSE::Pos(v, v, v));
        ++k;
      }
    });

    long long reads = 0;
    for (int i = 0; i < 1'000'000 && !torn.load(std::memory_order_relaxed); ++i) {
      const YSE::Pos f = YSE::Listener().forward();
      if (f.y != f.x + 1000.f || f.z != f.x + 2000.f) {
        torn.store(true, std::memory_order_relaxed);
      }
      const YSE::Pos u = YSE::Listener().upward();
      if (u.x != u.y || u.y != u.z) {
        torn.store(true, std::memory_order_relaxed);
      }
      ++reads;
    }

    stop.store(true, std::memory_order_release);
    orienter.join();

    CHECK_FALSE(torn.load(std::memory_order_relaxed));
    CHECK(reads > 0);

    // Restore the clean baseline (forward zero, up = +Y) other tests assume.
    YSE::Listener().orient(YSE::Pos(0.f, 0.f, 0.f), YSE::Pos(0.f, 1.f, 0.f));
  }

} // TEST_SUITE("listener")
