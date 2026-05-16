// Tier 1 microbenchmarks for the oscillator family.
//
// Two variants per generator:
//   - "FixedFreq"  — frequency is a compile-time-style scalar argument.
//   - "ModFreq"    — frequency is a per-sample buffer (FM / vibrato).
//
// The wavetable-backed `oscillator` is benchmarked against a band-limited
// saw table to reflect the realistic in-engine use of `wavetable::createSaw`.
//
// 1024-sample block for the same reason as filters — large enough that
// throughput dominates over per-call overhead.

#include "dsp/buffer.hpp"
#include "dsp/oscillators.hpp"
#include "dsp/wavetable.hpp"

#include <benchmark/benchmark.h>

namespace {

constexpr unsigned int kBlock = 1024;
constexpr float        kFreq = 440.0f;   // A4 — middle of the spectrum

} // namespace

// ── sine ──────────────────────────────────────────────────────────────────

static void BM_Sine_FixedFreq(benchmark::State& state) {
    YSE::DSP::sine osc;

    for (auto _ : state) {
        auto& out = osc(kFreq, kBlock);
        benchmark::DoNotOptimize(out.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_Sine_FixedFreq);

static void BM_Sine_ModFreq(benchmark::State& state) {
    YSE::DSP::sine osc;
    YSE::DSP::buffer freq(kBlock);
    freq = kFreq;  // constant-but-buffer-sourced frequency

    for (auto _ : state) {
        auto& out = osc(freq);
        benchmark::DoNotOptimize(out.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_Sine_ModFreq);

// ── saw (naive, not band-limited) ─────────────────────────────────────────

static void BM_Saw_FixedFreq(benchmark::State& state) {
    YSE::DSP::saw osc;

    for (auto _ : state) {
        auto& out = osc(kFreq, kBlock);
        benchmark::DoNotOptimize(out.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_Saw_FixedFreq);

static void BM_Saw_ModFreq(benchmark::State& state) {
    YSE::DSP::saw osc;
    YSE::DSP::buffer freq(kBlock);
    freq = kFreq;

    for (auto _ : state) {
        auto& out = osc(freq);
        benchmark::DoNotOptimize(out.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_Saw_ModFreq);

// ── oscillator (wavetable-driven) ─────────────────────────────────────────
//
// 32 harmonics in a 2048-sample table is a representative band-limited
// saw: 32 harmonics matches the alias-free range at typical mid-register
// pitches, and 2048 samples is the engine's STANDARD_BUFFERSIZE-aligned
// table length.

static void BM_Oscillator_FixedFreq(benchmark::State& state) {
    YSE::DSP::wavetable table;
    table.createSaw(32, 2048);

    YSE::DSP::oscillator osc;
    osc.initialize(table);

    for (auto _ : state) {
        auto& out = osc(kFreq, kBlock);
        benchmark::DoNotOptimize(out.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_Oscillator_FixedFreq);

static void BM_Oscillator_ModFreq(benchmark::State& state) {
    YSE::DSP::wavetable table;
    table.createSaw(32, 2048);

    YSE::DSP::oscillator osc;
    osc.initialize(table);

    YSE::DSP::buffer freq(kBlock);
    freq = kFreq;

    for (auto _ : state) {
        auto& out = osc(freq);
        benchmark::DoNotOptimize(out.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_Oscillator_ModFreq);

// ── noise (bonus — cheap to include and worth tracking) ──────────────────

static void BM_Noise(benchmark::State& state) {
    YSE::DSP::noise n;

    for (auto _ : state) {
        auto& out = n(kBlock);
        benchmark::DoNotOptimize(out.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_Noise);
