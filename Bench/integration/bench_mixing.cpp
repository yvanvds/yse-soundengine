// Tier 3 macro scenario — engine control-plane throughput at scale.
//
// Setup: 100 sounds attached across a master + 3-sub-channel tree, global
// reverb active. Drive `YSE::System().update()` in a tight loop and
// measure the per-call wall time. The metric is the cost of one engine
// "tick" with that scene state — message dispatch, state transitions,
// slow-pool task work, channel-tree traversal, manager bookkeeping.
//
// Scope caveat (carried over from the plan): the engine's audio device
// is paused throughout the benchmark, so the audio-callback's per-sample
// DSP path does NOT run. Tier 1 microbenchmarks cover DSP regressions
// directly; this file covers a different class of regression — anything
// that makes the *control plane* scale poorly with sound count. A 2x
// slowdown here in a 100-sound scene becomes 2x more frame-loop budget
// consumed by audio housekeeping, even if the audio thread itself stayed
// the same speed.

#include "yse.hpp"
#include "dsp/buffer.hpp"
#include "support/bench_helpers.hpp"

#include <benchmark/benchmark.h>

#include <array>
#include <memory>
#include <vector>

namespace {

constexpr int kNumSounds = 100;

// One source buffer shared by every sound — keeps the test memory
// footprint reasonable and matches the engine's normal buffer-sharing
// behaviour for sounds loaded from the same file. The buffer must
// outlive every sound that references it; static storage handles that.
YSE::DSP::buffer& sharedSourceBuffer() {
    // STANDARD_BUFFERSIZE is the engine's per-callback block; the source
    // length is independent of that. 4096 samples (~93 ms at the engine
    // rate) is long enough to keep sounds from completing within a
    // benchmark iteration.
    static YSE::DSP::buffer buf(4096);
    static bool initialised = false;
    if (!initialised) {
        buf = 0.25f;
        initialised = true;
    }
    return buf;
}

// Three sub-channels under the master, named for clarity in any future
// log output. Constructed lazily so they only exist if the macro
// benchmark actually runs.
struct ChannelTree {
    YSE::channel music;
    YSE::channel ambient;
    YSE::channel sfx;

    ChannelTree() {
        music   .create("bench.music",   YSE::ChannelMaster());
        ambient .create("bench.ambient", YSE::ChannelMaster());
        sfx     .create("bench.sfx",     YSE::ChannelMaster());
    }
};

ChannelTree& channelTree() {
    static ChannelTree t;
    return t;
}

// Sounds are heap-allocated and stored by unique_ptr so their addresses
// stay stable (the engine relies on the interface object's address for
// the impl back-pointer).
struct SoundPool {
    std::vector<std::unique_ptr<YSE::sound>> sounds;

    SoundPool() {
        auto& buf  = sharedSourceBuffer();
        auto& tree = channelTree();
        std::array<YSE::channel*, 3> channels { &tree.music, &tree.ambient, &tree.sfx };

        sounds.reserve(kNumSounds);
        for (int i = 0; i < kNumSounds; ++i) {
            auto s = std::make_unique<YSE::sound>();
            // Distribute round-robin across the three sub-channels.
            s->create(buf, channels[i % channels.size()],
                      /*loop=*/ true, /*volume=*/ 0.5f);
            s->play();
            sounds.push_back(std::move(s));
        }

        // Let the manager process the create/play burst before the
        // benchmark starts measuring. Without this, the first iteration
        // pays the entire setup cost and skews the average.
        for (int i = 0; i < 10; ++i) {
            YSE::System().update();
        }
    }
};

SoundPool& soundPool() {
    static SoundPool p;
    return p;
}

} // namespace

// ── System().update() with a 100-sound steady-state scene ────────────────

static void BM_Engine_UpdateTick_100Sounds(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }

    // Side-effect: lazy construction of the channel tree, sound pool, and
    // 10 warmup ticks happen here before the timed loop starts.
    (void) soundPool();

    for (auto _ : state) {
        YSE::System().update();
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Engine_UpdateTick_100Sounds);

// ── System().update() with a 100-sound scene + global reverb ─────────────

static void BM_Engine_UpdateTick_100Sounds_Reverb(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }

    (void) soundPool();
    YSE::System().getGlobalReverb().setActive(true);
    YSE::System().getGlobalReverb().setPreset(YSE::REVERB_HALL);

    // Drain the reverb-activation message before measuring.
    for (int i = 0; i < 5; ++i) YSE::System().update();

    for (auto _ : state) {
        YSE::System().update();
        benchmark::ClobberMemory();
    }

    // Leave the global reverb off for any benchmark that runs after this
    // one, so we don't pollute their measurements.
    YSE::System().getGlobalReverb().setActive(false);
}
BENCHMARK(BM_Engine_UpdateTick_100Sounds_Reverb);

// ── Listener position updates — the per-frame cost a game pays ───────────
//
// Updating Listener().pos() per frame is the dominant per-tick "control
// plane" cost in a real game. Bench it standalone so we can see if it
// stops being O(1).

static void BM_Engine_ListenerPosUpdate(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().init() failed");
        return;
    }
    float t = 0.0f;

    for (auto _ : state) {
        t += 0.01f;
        YSE::Listener().pos(YSE::Pos(t, 0.0f, 0.0f));
        benchmark::DoNotOptimize(&t);
    }
}
BENCHMARK(BM_Engine_ListenerPosUpdate);

// ── Audio-thread DSP throughput ───────────────────────────────────────────
//
// renderOffline(blocks) runs the same audio-callback body that PortAudio
// drives in production — master->dsp() + buffersToParent() per
// STANDARD_BUFFERSIZE block — synchronously on the bench thread, with no
// real audio device. This is the only place we measure the *audio-thread*
// cost (the channel-tree walk, sound rendering, channel mixing, reverb
// DSP). The UpdateTick_* benchmarks above measure the *control plane*
// cost — drain managers, dispatch messages, refresh timing — which is the
// other half of what runs per callback.
//
// Render counts are sized so each Google Benchmark iteration does enough
// work to dwarf the per-iteration overhead but completes well under a
// second. STANDARD_BUFFERSIZE = 128 samples at 44.1 kHz ≈ 2.9 ms of
// audio per block. 64 blocks per iteration ≈ 186 ms of "audio rendered",
// which is realistic for one engine update window and gives stable timing.

namespace {
constexpr int kBlocksPerIter = 64;
} // namespace

static void BM_Engine_RenderOffline_100Sounds(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().initOffline() failed");
        return;
    }
    (void) soundPool();

    // Warmup: the first render block pays an order-of-magnitude cost
    // over steady state (cold caches, sound state-transition burst, slow-
    // pool drain). Burn through that before timing.
    YSE::System().renderOffline(8);

    for (auto _ : state) {
        YSE::System().renderOffline(kBlocksPerIter);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kBlocksPerIter * YSE::STANDARD_BUFFERSIZE);
}
BENCHMARK(BM_Engine_RenderOffline_100Sounds);

static void BM_Engine_RenderOffline_100Sounds_Reverb(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().initOffline() failed");
        return;
    }
    (void) soundPool();
    YSE::System().getGlobalReverb().setActive(true);
    YSE::System().getGlobalReverb().setPreset(YSE::REVERB_HALL);

    // Drain the reverb-activation message before measuring.
    YSE::System().renderOffline(4);

    for (auto _ : state) {
        YSE::System().renderOffline(kBlocksPerIter);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kBlocksPerIter * YSE::STANDARD_BUFFERSIZE);

    // Reset so subsequent benchmarks don't inherit the reverb.
    YSE::System().getGlobalReverb().setActive(false);
}
BENCHMARK(BM_Engine_RenderOffline_100Sounds_Reverb);

// Real-time factor: how many seconds of audio render per second of
// wall-clock. RTF < 1 means the engine is faster than real-time. Larger
// block count than the other two so Google Benchmark sees enough work for
// a stable rate; reported in milliseconds.
static void BM_Engine_RealtimeFactor_100Sounds(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
        state.SkipWithError("YSE::System().initOffline() failed");
        return;
    }
    (void) soundPool();

    constexpr int blocks = 400;  // ≈ 1.16 s of audio at 44.1 kHz / 128
    for (auto _ : state) {
        YSE::System().renderOffline(blocks);
        benchmark::ClobberMemory();
    }
    const double audioSeconds =
        static_cast<double>(state.iterations() * blocks * YSE::STANDARD_BUFFERSIZE)
        / static_cast<double>(YSE::SAMPLERATE);
    state.counters["audio_seconds"] = audioSeconds;
    state.counters["realtime_factor"] = benchmark::Counter(
        audioSeconds, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}
BENCHMARK(BM_Engine_RealtimeFactor_100Sounds)->Unit(benchmark::kMillisecond);
