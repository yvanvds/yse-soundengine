// Tier 1 microbenchmarks for the per-sample DSP math classes.
//
// These are small utility blocks used as building stages in larger DSP
// chains. Individually cheap, but their cost adds up across a complex
// patcher graph — a 2x regression in any of them is worth catching.

#include "dsp/buffer.hpp"
#include "dsp/math.hpp"

#include <benchmark/benchmark.h>

namespace {

constexpr unsigned int kBlock = 1024;

// Reusable harness: each math class is invoked the same way (single-input
// `operator()(buffer&)`). Keeping the boilerplate in one place means
// adding a benchmark for another class is a one-liner.
template <typename Op>
void runUnary(benchmark::State& state, float initValue) {
    YSE::DSP::buffer in(kBlock);
    in = initValue;
    Op op;

    for (auto _ : state) {
        auto& out = op(in);
        benchmark::DoNotOptimize(out.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}

} // namespace

// ── clip — uses `clip::set` to configure thresholds before running ────────

static void BM_Clip(benchmark::State& state) {
    YSE::DSP::buffer in(kBlock);
    in = 0.8f;
    YSE::DSP::clip op;
    op.set(-0.5f, 0.5f);

    for (auto _ : state) {
        auto& out = op(in);
        benchmark::DoNotOptimize(out.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_Clip);

// ── sqrt / rSqrt — fast approximations vs std lib expectations ───────────
//
// Input is 4.0 so the result (2.0 for sqrt, 0.5 for rSqrt) is well within
// normal range and the math doesn't accidentally hit any denormal-handling
// slow paths.

static void BM_Sqrt(benchmark::State& s)  { runUnary<YSE::DSP::sqrt>(s, 4.0f); }
static void BM_RSqrt(benchmark::State& s) { runUnary<YSE::DSP::rSqrt>(s, 4.0f); }
BENCHMARK(BM_Sqrt);
BENCHMARK(BM_RSqrt);

// ── wrap — fractional part of a value > 1.0 ──────────────────────────────

static void BM_Wrap(benchmark::State& s) { runUnary<YSE::DSP::wrap>(s, 3.7f); }
BENCHMARK(BM_Wrap);

// ── MIDI ↔ frequency conversions ──────────────────────────────────────────
//
// Use middle C (MIDI 60) as the input so the conversion result is the
// well-known 261.63 Hz — keeps any future debugging trivially verifiable.

static void BM_MidiToFreq(benchmark::State& s) { runUnary<YSE::DSP::midiToFreq>(s, 60.0f); }
static void BM_FreqToMidi(benchmark::State& s) { runUnary<YSE::DSP::freqToMidi>(s, 440.0f); }
BENCHMARK(BM_MidiToFreq);
BENCHMARK(BM_FreqToMidi);

// ── dB ↔ amplitude conversions ────────────────────────────────────────────

static void BM_DbToRms(benchmark::State& s) { runUnary<YSE::DSP::dbToRms>(s, -6.0f);   }
static void BM_RmsToDb(benchmark::State& s) { runUnary<YSE::DSP::rmsToDb>(s,  0.5f);   }
BENCHMARK(BM_DbToRms);
BENCHMARK(BM_RmsToDb);
