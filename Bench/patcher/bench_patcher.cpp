// Tier 2 microbenchmarks for the patcher control plane.
//
// The patcher's audio-thread side (`patcher::process` etc.) is internal and
// not directly callable from outside the engine; this file therefore
// benchmarks the public surface that user code hits in real applications:
//   - graph construction (CreateObject + Connect)
//   - JSON dump/parse round-trip
//   - inbound message dispatch (PassBang / PassData)
//
// Each of these is called by game code at varying frequencies — typically
// graph construction once at scene load, JSON round-trips on save / load,
// and PassData every frame for any patcher-driven parameter. A regression
// in any of them maps directly to frame budget consumed by the audio
// system.

#include "yse.hpp"
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "support/bench_helpers.hpp"

#include <benchmark/benchmark.h>

#include <string>

namespace {

// Build a representative 5-object signal-flow graph:
//   sine → biquad → delay → DAC, with a receive object for external control.
// Returns the constructed patcher by reference so callers can drive it
// further. Each call wipes and rebuilds the graph, which is the cost we
// actually want to measure in the construction benchmark.
void buildSmallGraph(YSE::patcher& p) {
    p.Clear();

    auto* sine    = p.CreateObject(YSE::OBJ::D_SINE,    "440");
    auto* biquad  = p.CreateObject(YSE::OBJ::D_LOWPASS, "2000");
    auto* dac     = p.CreateObject(YSE::OBJ::D_DAC);
    auto* receive = p.CreateObject(YSE::OBJ::G_RECEIVE, "freq");

    p.Connect(sine,    0, biquad, 0);
    p.Connect(biquad,  0, dac,    0);
    p.Connect(receive, 0, sine,   0);  // external control: write to "freq"
}

} // namespace

// ── Graph construction ───────────────────────────────────────────────────

static void BM_Patcher_BuildSmallGraph(benchmark::State& state) {
    if (!BenchHelpers::engineInit()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    YSE::patcher p;
    p.create(2);

    for (auto _ : state) {
        buildSmallGraph(p);
        benchmark::DoNotOptimize(&p);
    }
}
BENCHMARK(BM_Patcher_BuildSmallGraph);

// ── CreateObject only — isolates per-object construction cost ────────────

static void BM_Patcher_CreateObject(benchmark::State& state) {
    if (!BenchHelpers::engineInit()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    YSE::patcher p;
    p.create(2);

    for (auto _ : state) {
        auto* h = p.CreateObject(YSE::OBJ::D_SINE, "440");
        benchmark::DoNotOptimize(h);
        // Keep the patcher from growing without bound — if we let it,
        // later iterations would see a larger object list and the
        // per-create cost would creep up across the run.
        p.Clear();
    }
}
BENCHMARK(BM_Patcher_CreateObject);

// ── Connect — pure edge-insertion cost ───────────────────────────────────

static void BM_Patcher_Connect(benchmark::State& state) {
    if (!BenchHelpers::engineInit()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    YSE::patcher p;
    p.create(2);

    auto* a = p.CreateObject(YSE::OBJ::D_SINE,    "440");
    auto* b = p.CreateObject(YSE::OBJ::D_LOWPASS, "2000");

    for (auto _ : state) {
        p.Connect(a, 0, b, 0);
        // Disconnect immediately so the next iteration sees the same
        // starting state (no degenerate "edge already exists" fast path).
        p.Disconnect(a, 0, b, 0);
        benchmark::DoNotOptimize(&p);
    }
}
BENCHMARK(BM_Patcher_Connect);

// ── JSON serialise ───────────────────────────────────────────────────────

static void BM_Patcher_DumpJSON(benchmark::State& state) {
    if (!BenchHelpers::engineInit()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    YSE::patcher p;
    p.create(2);
    buildSmallGraph(p);

    for (auto _ : state) {
        std::string s = p.DumpJSON();
        benchmark::DoNotOptimize(s);
    }
}
BENCHMARK(BM_Patcher_DumpJSON);

// ── JSON parse — load preset, the common save-state path ─────────────────

static void BM_Patcher_ParseJSON(benchmark::State& state) {
    if (!BenchHelpers::engineInit()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    YSE::patcher source;
    source.create(2);
    buildSmallGraph(source);
    const std::string json = source.DumpJSON();

    YSE::patcher target;
    target.create(2);

    for (auto _ : state) {
        target.ParseJSON(json);
        benchmark::DoNotOptimize(&target);
    }
}
BENCHMARK(BM_Patcher_ParseJSON);

// ── External message dispatch ────────────────────────────────────────────

static void BM_Patcher_PassFloat(benchmark::State& state) {
    if (!BenchHelpers::engineInit()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    YSE::patcher p;
    p.create(2);
    buildSmallGraph(p);  // has a `.r freq` receive object

    float v = 0.0f;
    for (auto _ : state) {
        v += 1.0f;
        if (v > 2000.0f) v = 100.0f;
        bool ok = p.PassData(v, "freq");
        benchmark::DoNotOptimize(ok);
    }
}
BENCHMARK(BM_Patcher_PassFloat);
