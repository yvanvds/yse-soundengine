// Tier 1 microbenchmarks for the variable-length delay line.
//
// Three things matter for catching regressions:
//   - `process()` — the write half, runs every callback for every delay-
//     enabled sound.
//   - `read()` fixed-offset — chorus / static-delay use case.
//   - `read()` modulated — flanger / pitch-shift / Doppler use case;
//     fractional positions exercise the interpolation path.
//
// A 44100-sample line (~1 s at engine rate) is large enough that the
// fractional-position cache traffic is realistic but not so large that
// allocation dominates the setup.

#include "dsp/buffer.hpp"
#include "dsp/delay.hpp"

#include <benchmark/benchmark.h>

namespace {

constexpr unsigned int kBlock     = 1024;
constexpr int          kLineSize  = 44100;       // ~1 s at the engine rate
constexpr unsigned int kFixedTap  = 22050;       // ~500 ms — well inside the line

} // namespace

// ── process (write half) ──────────────────────────────────────────────────

static void BM_Delay_Process(benchmark::State& state) {
    YSE::DSP::delay d(kLineSize);
    YSE::DSP::buffer in(kBlock);
    in = 0.5f;

    for (auto _ : state) {
        d.process(in);
        benchmark::DoNotOptimize(&d);
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_Delay_Process);

// ── read at a fixed offset ────────────────────────────────────────────────

static void BM_Delay_ReadFixed(benchmark::State& state) {
    YSE::DSP::delay d(kLineSize);
    YSE::DSP::buffer in(kBlock);
    YSE::DSP::buffer out(kBlock);
    in = 0.5f;

    // Prime the line so reads return real audio rather than zeros.
    for (int i = 0; i < kLineSize / static_cast<int>(kBlock) + 1; ++i) {
        d.process(in);
    }

    for (auto _ : state) {
        d.read(out, kFixedTap);
        benchmark::DoNotOptimize(out.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_Delay_ReadFixed);

// ── read at a modulated (per-sample) offset ──────────────────────────────
//
// The modulated overload reads at fractional positions and interpolates,
// so its per-sample cost is materially higher than the fixed variant.

static void BM_Delay_ReadModulated(benchmark::State& state) {
    YSE::DSP::delay d(kLineSize);
    YSE::DSP::buffer in(kBlock);
    YSE::DSP::buffer out(kBlock);
    YSE::DSP::buffer delayTimes(kBlock);
    in = 0.5f;
    // Constant-valued buffer keeps the interpolation work the same per
    // sample without giving the optimiser a hint that the read offset is
    // invariant — the engine never sees that pattern in production but
    // the cache / memory behaviour is identical to a slowly-varying LFO.
    delayTimes = static_cast<float>(kFixedTap);

    // Prime the line as above.
    for (int i = 0; i < kLineSize / static_cast<int>(kBlock) + 1; ++i) {
        d.process(in);
    }

    for (auto _ : state) {
        d.read(out, delayTimes);
        benchmark::DoNotOptimize(out.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_Delay_ReadModulated);
