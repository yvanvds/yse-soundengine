// Phase 1 proof-of-concept benchmark — exercises one hot-path DSP loop so
// the rest of the toolchain (CMake preset, FetchContent of google-benchmark,
// linkage to yse_objects, CI workflow) has something real to measure.
//
// Buffer * scalar is one of the simplest per-sample operations in the engine
// and is reached from every gain stage in the DSP chain, which makes it a
// reasonable canary for arithmetic-loop regressions.

#include "dsp/buffer.hpp"

#include <benchmark/benchmark.h>

static void BM_BufferMulScalar(benchmark::State& state) {
    const auto length = static_cast<unsigned int>(state.range(0));

    // Construct outside the measurement loop — the buffer ctor allocates
    // from the heap, which would dominate the timing for small block sizes.
    YSE::DSP::buffer buf(length);
    buf = 0.5f;  // start with non-zero data so the multiply does real work

    for (auto _ : state) {
        buf *= 0.999f;
        benchmark::DoNotOptimize(buf.getPtr());
    }

    state.SetItemsProcessed(state.iterations() * length);
    state.SetBytesProcessed(state.iterations() * length * sizeof(float));
}

// Realistic audio block sizes: 128 (low-latency game audio), 512 (default
// engine buffer), 1024 (offline render / generous latency).
BENCHMARK(BM_BufferMulScalar)->Arg(128)->Arg(512)->Arg(1024);
