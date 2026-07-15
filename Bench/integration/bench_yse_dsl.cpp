// Tier 3 macro scenario — live-coding DSL round-trip latency (issue #126).
//
// The DSL spec (docs/design/live_coding_dsl.md) promises a yse.send published
// from the script thread reaches its bus subscriber within one engine update
// tick. This benchmark measures that round trip end to end: submit a one-line
// script through the C API (yse_run_script), then drive System().update() until
// a C++ bus subscriber observes the value, timing the whole path —
//   yse_run_script enqueue -> script-thread wake -> exec -> T_GUI dispatch ->
//   C++ subscriber.
//
// Each timed iteration also counts how many update() ticks the round trip
// needed; the count is reported as the "ticks" counter so a regression that
// pushes delivery past one tick shows up as ticks > 1, independently of the
// wall-clock number (which is dominated by the pump loop's sleep granularity).
//
// Only built when YSE_ENABLE_PYTHON is ON; otherwise this TU is empty.

#if YSE_ENABLE_PYTHON

#include "yse.hpp"
#include "yse_c/yse_python.h"
#include "internal/namedBus.h"
#include "support/bench_helpers.hpp"

#include <benchmark/benchmark.h>

#include <atomic>
#include <string>

namespace {

// Round-trip latency: script-thread yse.send -> C++ subscriber.
void BM_Dsl_SendRoundTrip(benchmark::State& state) {
  if (!BenchHelpers::engineInit()) {
    state.SkipWithError("YSE::System().init() failed");
    return;
  }

  std::atomic<long long> received{0};
  YSE::INTERNAL::SubHandle handle = YSE::INTERNAL::Bus().subscribe(
      "bench.dsl.rt", [&](const YSE::INTERNAL::BusValue&) { received.fetch_add(1); });

  // Warm the interpreter + binding import so the first timed iteration does not
  // pay one-time import cost.
  yse_run_script("yse.send('bench.dsl.rt', 0)\n");
  for (int i = 0; i < 50 && received.load() == 0; ++i) {
    YSE::System().update();
    YSE::System().sleep(1);
  }

  long long totalTicks = 0;
  int seq = 1;
  for (auto _ : state) {
    const long long target = received.load() + 1;
    std::string src = "yse.send('bench.dsl.rt', " + std::to_string(seq++) + ")\n";
    yse_run_script(src.c_str());

    int ticks = 0;
    while (received.load() < target) {
      YSE::System().update();
      ++ticks;
      if (ticks > 200) break;  // safety valve; should resolve in ~1 tick
      YSE::System().sleep(1);
    }
    totalTicks += ticks;
    benchmark::ClobberMemory();
  }

  state.counters["ticks"] = benchmark::Counter(
      static_cast<double>(totalTicks) / static_cast<double>(state.iterations()));

  YSE::INTERNAL::Bus().unsubscribe(handle);
}
BENCHMARK(BM_Dsl_SendRoundTrip)->Iterations(BenchHelpers::kLeakyBenchIterations / 100);

} // namespace

#endif  // YSE_ENABLE_PYTHON
