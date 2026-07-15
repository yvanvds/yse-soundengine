// Tier 1 microbenchmark for YSE::mpmcQueue (utils/mpmcQueue.hpp) under
// producer/consumer contention.
//
// This is the workload that cell false sharing degrades (issue #288): several
// worker producers and consumers hammer sequential ring cells, so cells that
// share a 64-byte line ping-pong between cores. The multi-producer /
// multi-consumer cases mirror the render-pool fan-out — the audio callback and
// nested worker dispatch pushing DSP jobs while the workers pop them. It is the
// benchmark that justifies (or refutes) padding cells to a cache line.

#include "utils/mpmcQueue.hpp"

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

  // Spawn `producers` threads that each push `kPerProducer` items and `consumers`
  // threads that drain until every item is consumed. Item count is kept high so
  // thread spawn/join cost is amortised away and the timing reflects queue ops.
  void runContention(benchmark::State& state, int producers, int consumers) {
    constexpr int kPerProducer = 100000;
    const std::int64_t total = static_cast<std::int64_t>(producers) * kPerProducer;

    for (auto _ : state) {
      YSE::mpmcQueue<void*> q(1024);
      std::atomic<int> consumed{0};
      std::atomic<bool> producersDone{false};

      std::vector<std::thread> threads;
      threads.reserve(static_cast<std::size_t>(producers) + consumers);

      for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&] {
          void* v = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
          for (int i = 0; i < kPerProducer; ++i) {
            while (!q.try_push(v))
              std::this_thread::yield();
          }
        });
      }
      for (int c = 0; c < consumers; ++c) {
        threads.emplace_back([&] {
          void* v = nullptr;
          for (;;) {
            if (q.try_pop(v)) {
              consumed.fetch_add(1, std::memory_order_relaxed);
            } else if (producersDone.load(std::memory_order_acquire)) {
              if (!q.try_pop(v)) break;
              consumed.fetch_add(1, std::memory_order_relaxed);
            } else {
              std::this_thread::yield();
            }
          }
        });
      }

      for (int p = 0; p < producers; ++p)
        threads[static_cast<std::size_t>(p)].join();
      producersDone.store(true, std::memory_order_release);
      for (int c = producers; c < producers + consumers; ++c)
        threads[static_cast<std::size_t>(c)].join();

      benchmark::DoNotOptimize(consumed.load());
    }

    state.SetItemsProcessed(state.iterations() * total);
  }

} // namespace

// Single producer / single consumer — low contention baseline.
static void BM_MpmcQueue_1P1C(benchmark::State& state) {
  runContention(state, 1, 1);
}
BENCHMARK(BM_MpmcQueue_1P1C)->UseRealTime();

// Render fan-out: multiple worker producers and consumers on adjacent cells.
static void BM_MpmcQueue_4P4C(benchmark::State& state) {
  runContention(state, 4, 4);
}
BENCHMARK(BM_MpmcQueue_4P4C)->UseRealTime();

// Heavy contention — stresses the false-sharing path hardest.
static void BM_MpmcQueue_8P8C(benchmark::State& state) {
  runContention(state, 8, 8);
}
BENCHMARK(BM_MpmcQueue_8P8C)->UseRealTime();
