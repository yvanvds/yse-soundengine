// Tier 1 microbenchmarks for `YSE::DSP::buffer` arithmetic — the per-sample
// loops that fire on every callback for every channel. Regressions here
// scale linearly with sound count, which makes the buffer ops one of the
// highest-ROI things to gate.
//
// Coverage:
//   - operator+= / -= / *= / /= with a scalar RHS
//   - operator+= / -= / *= / /= with a buffer RHS
//
// Block sizes 128 / 512 / 1024 match low-latency game audio, the default
// engine buffer, and offline / generous-latency setups respectively. The
// per-sample cost drops as the loop overhead amortizes across larger
// blocks; tracking all three exposes when a change shifts the
// throughput / latency tradeoff.

#include "dsp/buffer.hpp"

#include <benchmark/benchmark.h>

namespace {

// One templated body per operation flavour so the report shows the
// operator and the RHS kind without 16 near-identical functions.
//
// `Op` is a function-object (lambda) that runs the operation once on
// `dst` and (optionally) `src` — the compiler inlines it through the
// state loop.

template <typename Op>
void runScalarOp(benchmark::State& state, Op op) {
    const auto length = static_cast<unsigned int>(state.range(0));
    YSE::DSP::buffer dst(length);
    dst = 0.5f;

    for (auto _ : state) {
        op(dst);
        benchmark::DoNotOptimize(dst.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * length);
    state.SetBytesProcessed(state.iterations() * length * sizeof(float));
}

template <typename Op>
void runBufferOp(benchmark::State& state, Op op) {
    const auto length = static_cast<unsigned int>(state.range(0));
    YSE::DSP::buffer dst(length);
    YSE::DSP::buffer src(length);
    dst = 0.5f;
    src = 0.25f;

    for (auto _ : state) {
        op(dst, src);
        benchmark::DoNotOptimize(dst.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * length);
    state.SetBytesProcessed(state.iterations() * length * sizeof(float));
}

} // namespace

// ── Scalar RHS ────────────────────────────────────────────────────────────

static void BM_BufferAddScalar(benchmark::State& s) { runScalarOp(s, [](auto& d){ d += 0.001f; }); }
static void BM_BufferSubScalar(benchmark::State& s) { runScalarOp(s, [](auto& d){ d -= 0.001f; }); }
static void BM_BufferMulScalar(benchmark::State& s) { runScalarOp(s, [](auto& d){ d *= 0.999f; }); }
static void BM_BufferDivScalar(benchmark::State& s) { runScalarOp(s, [](auto& d){ d /= 1.001f; }); }

BENCHMARK(BM_BufferAddScalar)->Arg(128)->Arg(512)->Arg(1024);
BENCHMARK(BM_BufferSubScalar)->Arg(128)->Arg(512)->Arg(1024);
BENCHMARK(BM_BufferMulScalar)->Arg(128)->Arg(512)->Arg(1024);
BENCHMARK(BM_BufferDivScalar)->Arg(128)->Arg(512)->Arg(1024);

// ── Buffer RHS ────────────────────────────────────────────────────────────

static void BM_BufferAddBuffer(benchmark::State& s) { runBufferOp(s, [](auto& d, auto& src){ d += src; }); }
static void BM_BufferSubBuffer(benchmark::State& s) { runBufferOp(s, [](auto& d, auto& src){ d -= src; }); }
static void BM_BufferMulBuffer(benchmark::State& s) { runBufferOp(s, [](auto& d, auto& src){ d *= src; }); }
static void BM_BufferDivBuffer(benchmark::State& s) { runBufferOp(s, [](auto& d, auto& src){ d /= src; }); }

BENCHMARK(BM_BufferAddBuffer)->Arg(128)->Arg(512)->Arg(1024);
BENCHMARK(BM_BufferSubBuffer)->Arg(128)->Arg(512)->Arg(1024);
BENCHMARK(BM_BufferMulBuffer)->Arg(128)->Arg(512)->Arg(1024);
BENCHMARK(BM_BufferDivBuffer)->Arg(128)->Arg(512)->Arg(1024);
