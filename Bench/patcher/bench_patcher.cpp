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
//
// ── Why these benches recycle the patcher in batches (issue #315) ─────────
//
// Since the GraphState rework (issue #226), every structural edit
// (CreateObject / Connect / Disconnect / Clear / ParseJSON) compiles and
// publishes a fresh immutable snapshot and retires the previous one. Two
// consequences for a benchmark loop, neither of which production code hits:
//
//  1. Retired snapshots/objects are reclaimed only after the patcher's
//     audio-thread block counter advances past their retirement epoch
//     (issue #227). These benches run with no audio thread at all
//     (offline init, nothing renders the patcher), so the epoch never
//     advances and *every* retired snapshot is held until the patcher
//     itself is destroyed.
//
//  2. Graph ids are stamped once per inlet/outlet and never reused within
//     a patcherImplementation, and every GraphState allocates id-indexed
//     tables. A loop that keeps creating objects therefore makes each
//     successive snapshot bigger, so cost and retained memory grow
//     quadratically with iteration count.
//
// At the previous 100k-iteration cap this added up to a multi-GB,
// effectively never-finishing run that took down the CI bench lane on
// every dev push (issue #315). Recycling the patcher every kEditBatch
// iterations — outside the timed region; its destructor frees all retired
// state and resets the id counters — bounds both effects while keeping
// the measured per-iteration operation identical.

#include "yse.hpp"
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "support/bench_helpers.hpp"

#include <benchmark/benchmark.h>

#include <memory>
#include <string>

namespace {

// Iterations between patcher recycles. Keeps the per-batch id count (and
// with it the size of every snapshot built in the batch) small and bounds
// un-reclaimed retired state to a couple of MB. Also deliberately below
// the patcher's 256-slot value-queue capacity so BM_Patcher_PassFloat
// always measures the real enqueue path, never the queue-full drop+log
// path (with no audio thread the queue is never drained).
constexpr int kEditBatch = 64;

// Iteration cap for the structural-edit benches. They cannot use
// google-benchmark's default min-time autoscaling for the reasons above,
// and BenchHelpers::kLeakyBenchIterations (100k) is far more than needed
// for a stable aggregate on µs-scale ops: 8192 keeps each repetition in
// the tens-of-milliseconds range while the 3-repetition aggregates stay
// well inside the bench lane's 110% alert threshold.
constexpr int kEditIterations = 8192;

std::unique_ptr<YSE::patcher> makePatcher() {
    auto p = std::make_unique<YSE::patcher>();
    p->create(2);
    return p;
}

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
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    auto p = makePatcher();

    int inBatch = 0;
    for (auto _ : state) {
        if (inBatch == kEditBatch) {
            state.PauseTiming();
            p = makePatcher();  // frees all retired state, resets graph ids
            inBatch = 0;
            state.ResumeTiming();
        }
        ++inBatch;
        buildSmallGraph(*p);
        benchmark::DoNotOptimize(p.get());
    }
}
BENCHMARK(BM_Patcher_BuildSmallGraph)->Iterations(kEditIterations);

// ── CreateObject only — isolates per-object construction cost ────────────

static void BM_Patcher_CreateObject(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    auto p = makePatcher();

    int inBatch = 0;
    for (auto _ : state) {
        if (inBatch == kEditBatch) {
            state.PauseTiming();
            p = makePatcher();
            inBatch = 0;
            state.ResumeTiming();
        }
        ++inBatch;
        auto* h = p->CreateObject(YSE::OBJ::D_SINE, "440");
        benchmark::DoNotOptimize(h);
        // Keep the patcher from growing without bound — if we let it,
        // later iterations would see a larger object list and the
        // per-create cost would creep up across the run.
        p->Clear();
    }
}
BENCHMARK(BM_Patcher_CreateObject)->Iterations(kEditIterations);

// ── Connect — pure edge-insertion cost ───────────────────────────────────

static void BM_Patcher_Connect(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    auto p = makePatcher();
    auto* a = p->CreateObject(YSE::OBJ::D_SINE,    "440");
    auto* b = p->CreateObject(YSE::OBJ::D_LOWPASS, "2000");

    // The two objects live for the whole batch, so ids stay constant here;
    // the recycle only bounds the retired snapshots the edits produce.
    int inBatch = 0;
    for (auto _ : state) {
        if (inBatch == kEditBatch) {
            state.PauseTiming();
            p = makePatcher();
            a = p->CreateObject(YSE::OBJ::D_SINE,    "440");
            b = p->CreateObject(YSE::OBJ::D_LOWPASS, "2000");
            inBatch = 0;
            state.ResumeTiming();
        }
        ++inBatch;
        p->Connect(a, 0, b, 0);
        // Disconnect immediately so the next iteration sees the same
        // starting state (no degenerate "edge already exists" fast path).
        p->Disconnect(a, 0, b, 0);
        benchmark::DoNotOptimize(p.get());
    }
}
BENCHMARK(BM_Patcher_Connect)->Iterations(kEditIterations);

// ── JSON serialise ───────────────────────────────────────────────────────

static void BM_Patcher_DumpJSON(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    // DumpJSON does no structural edits, so no recycling is needed: nothing
    // is retired and the default min-time iteration count is safe.
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
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    YSE::patcher source;
    source.create(2);
    buildSmallGraph(source);
    const std::string json = source.DumpJSON();

    auto target = makePatcher();

    int inBatch = 0;
    for (auto _ : state) {
        if (inBatch == kEditBatch) {
            state.PauseTiming();
            target = makePatcher();
            inBatch = 0;
            state.ResumeTiming();
        }
        ++inBatch;
        target->ParseJSON(json);
        benchmark::DoNotOptimize(target.get());
    }
}
BENCHMARK(BM_Patcher_ParseJSON)->Iterations(kEditIterations);

// ── External message dispatch ────────────────────────────────────────────

static void BM_Patcher_PassFloat(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    auto p = makePatcher();
    buildSmallGraph(*p);  // has a `.r freq` receive object

    // PassData enqueues on a fixed 256-slot queue that only the (absent)
    // audio thread drains. Recycling every kEditBatch (< 256) iterations
    // keeps the queue from filling, so every iteration measures the real
    // enqueue path rather than the drop-and-log backpressure path.
    int inBatch = 0;
    float v = 0.0f;
    for (auto _ : state) {
        if (inBatch == kEditBatch) {
            state.PauseTiming();
            p = makePatcher();
            buildSmallGraph(*p);
            inBatch = 0;
            state.ResumeTiming();
        }
        ++inBatch;
        v += 1.0f;
        if (v > 2000.0f) v = 100.0f;
        bool ok = p->PassData(v, "freq");
        benchmark::DoNotOptimize(ok);
    }
}
BENCHMARK(BM_Patcher_PassFloat)->Iterations(kEditIterations);
