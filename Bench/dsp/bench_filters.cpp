// Tier 1 microbenchmarks for the four public single-channel filters.
//
// Each filter mutates the input buffer in place via a stateful IIR loop. A
// fixed cutoff / Q / gain is configured once before the measurement; the
// filter coefficients then remain constant across iterations, so the
// timing reflects the per-sample math only — not coefficient recomputation.
//
// A 1024-sample block matches a generous-latency engine setup and is large
// enough for vectoriser-friendly throughput to dominate over per-call
// overhead, which is the regime we care about.

#include "dsp/buffer.hpp"
#include "dsp/filters.hpp"
#include "headers/enums.hpp"

#include <benchmark/benchmark.h>

namespace {

constexpr unsigned int kBlock = 1024;
constexpr float        kCutoffHz = 1000.0f;
constexpr float        kQ = 0.707f;  // -3 dB at the cutoff (Butterworth)

// Fill the test buffer with a value that survives repeated low-pass /
// high-pass filtering without converging to zero or denormal range.
// 0.5 keeps the steady-state response well clear of the denormal cliff
// (~1e-38 for float) so the timing isn't polluted by hardware-level
// denormal-handling slowdowns.
constexpr float kSignal = 0.5f;

} // namespace

// ── highPass ──────────────────────────────────────────────────────────────

static void BM_HighPass(benchmark::State& state) {
    YSE::DSP::buffer buf(kBlock);
    YSE::DSP::highPass hp;
    hp.setFrequency(kCutoffHz);
    buf = kSignal;

    for (auto _ : state) {
        hp(buf);
        benchmark::DoNotOptimize(buf.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_HighPass);

// ── lowPass ───────────────────────────────────────────────────────────────

static void BM_LowPass(benchmark::State& state) {
    YSE::DSP::buffer buf(kBlock);
    YSE::DSP::lowPass lp;
    lp.setFrequency(kCutoffHz);
    buf = kSignal;

    for (auto _ : state) {
        lp(buf);
        benchmark::DoNotOptimize(buf.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_LowPass);

// ── bandPass ──────────────────────────────────────────────────────────────

static void BM_BandPass(benchmark::State& state) {
    YSE::DSP::buffer buf(kBlock);
    YSE::DSP::bandPass bp;
    bp.set(kCutoffHz, kQ);
    buf = kSignal;

    for (auto _ : state) {
        bp(buf);
        benchmark::DoNotOptimize(buf.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_BandPass);

// ── biQuad ────────────────────────────────────────────────────────────────
//
// Run each of the seven BQ_TYPE configurations as a separate benchmark so a
// regression localised to one shape (e.g. peak vs lowpass) is visible
// instead of being averaged out. Same cutoff / Q across all modes for
// comparable numbers.

namespace {

void runBiQuad(benchmark::State& state, YSE::BQ_TYPE type) {
    YSE::DSP::buffer buf(kBlock);
    YSE::DSP::biQuad bq;
    bq.set(type, kCutoffHz, kQ);
    buf = kSignal;

    for (auto _ : state) {
        bq(buf);
        benchmark::DoNotOptimize(buf.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}

} // namespace

static void BM_BiQuad_Lowpass  (benchmark::State& s) { runBiQuad(s, YSE::BQ_LOWPASS);  }
static void BM_BiQuad_Highpass (benchmark::State& s) { runBiQuad(s, YSE::BQ_HIGHPASS); }
static void BM_BiQuad_Bandpass (benchmark::State& s) { runBiQuad(s, YSE::BQ_BANDPASS); }
static void BM_BiQuad_Notch    (benchmark::State& s) { runBiQuad(s, YSE::BQ_NOTCH);    }
static void BM_BiQuad_Peak     (benchmark::State& s) { runBiQuad(s, YSE::BQ_PEAK);     }
static void BM_BiQuad_Lowshelf (benchmark::State& s) { runBiQuad(s, YSE::BQ_LOWSHELF); }
static void BM_BiQuad_Highshelf(benchmark::State& s) { runBiQuad(s, YSE::BQ_HIGHSHELF);}

BENCHMARK(BM_BiQuad_Lowpass);
BENCHMARK(BM_BiQuad_Highpass);
BENCHMARK(BM_BiQuad_Bandpass);
BENCHMARK(BM_BiQuad_Notch);
BENCHMARK(BM_BiQuad_Peak);
BENCHMARK(BM_BiQuad_Lowshelf);
BENCHMARK(BM_BiQuad_Highshelf);
