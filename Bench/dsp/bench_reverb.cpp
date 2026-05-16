// Tier 2 microbenchmarks for the reverb config path.
//
// Scope note: the Freeverb DSP itself lives in YSE::INTERNAL and is driven
// by the audio callback. There is no public entry point to "process one
// 1024-sample block through reverb" — so this file benchmarks the public
// configuration surface (the `YSE::reverb` setter chain plus the global
// reverb singleton). Regressions to the actual reverb DSP are caught
// indirectly by integration/bench_mixing.cpp, which measures engine-level
// throughput while the global reverb is active.
//
// The config-path benchmarks below catch a different class of regression:
//   - lock-free message queue overhead on the reverb manager
//   - atomic-state churn in REVERB::implementationObject
//   - any allocation that creeps into the setter chain
// In a game that moves the listener through reverb zones every frame,
// these calls fire at frame rate; a slowdown here translates to lost
// frame budget.

#include "yse.hpp"
#include "headers/enums.hpp"
#include "support/bench_helpers.hpp"

#include <benchmark/benchmark.h>

namespace {

// One reverb object per benchmark process — engine teardown happens at
// process exit, so we share a single reverb across all iterations rather
// than allocate / release in the timed loop (which would dominate the
// measurement).
YSE::reverb& reverbForBench() {
    static YSE::reverb r;
    static bool created = false;
    if (!created) {
        r.create();
        created = true;
    }
    return r;
}

} // namespace

// ── setPosition — the high-frequency call in a moving-listener scenario ──

static void BM_Reverb_SetPosition(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    auto& r = reverbForBench();
    float t = 0.0f;

    for (auto _ : state) {
        // Walk along a deterministic path so each call hands the manager a
        // genuinely new value (avoids any "skip the message if unchanged"
        // fast path that might exist now or be added later).
        t += 0.01f;
        r.setPosition(YSE::Pos(t, 0.0f, 0.0f));
        benchmark::DoNotOptimize(&r);
    }
}
BENCHMARK(BM_Reverb_SetPosition);

// ── setPreset — relatively rare but worth tracking for sudden cost spikes ─

static void BM_Reverb_SetPreset(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    auto& r = reverbForBench();
    const YSE::REVERB_PRESET presets[] = {
        YSE::REVERB_GENERIC,
        YSE::REVERB_HALL,
        YSE::REVERB_CAVE,
        YSE::REVERB_BATHROOM,
    };
    size_t i = 0;

    for (auto _ : state) {
        r.setPreset(presets[i & 3]);
        ++i;
        benchmark::DoNotOptimize(&r);
    }
}
BENCHMARK(BM_Reverb_SetPreset);

// ── setDryWetBalance — fired every frame by listener / reverb-zone blend ─

static void BM_Reverb_SetDryWetBalance(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    auto& r = reverbForBench();
    float mix = 0.0f;

    for (auto _ : state) {
        // Sweep dry/wet so the value actually changes each iteration.
        mix += 0.001f;
        if (mix >= 1.0f) mix = 0.0f;
        r.setDryWetBalance(1.0f - mix, mix);
        benchmark::DoNotOptimize(&r);
    }
}
BENCHMARK(BM_Reverb_SetDryWetBalance);

// ── global reverb on/off ─────────────────────────────────────────────────
//
// Toggling the global reverb is rare in practice (typically once per scene
// load) but pathologically expensive operations here would suggest the
// activation path has gained allocator pressure or lock contention.

static void BM_Reverb_GlobalToggle(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    bool on = false;

    for (auto _ : state) {
        on = !on;
        YSE::System().getGlobalReverb().setActive(on);
        benchmark::DoNotOptimize(on);
    }
}
BENCHMARK(BM_Reverb_GlobalToggle);
