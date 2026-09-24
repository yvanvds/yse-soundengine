// Tier 3 macro scenarios — heavy-per-voice render scenes (issue #857, the
// measurement foundation of the render-scheduler epic #856).
//
// Every earlier render benchmark renders sounds that cost ~70 ns per block
// (128 samples copied out of a shared constant buffer), and #812's "heavy"
// 1600-sound scene still came out at ~15 us per channel job — too small for
// any handoff to amortise, so it could not say whether parallel rendering
// helps. The scenes here give every sound a real voice's DSP load instead:
// a dspSourceObject running a sawtooth oscillator into a Moog ladder filter
// whose cutoff sweeps every block, under a one-pole amplitude envelope. That
// is one VA-synth voice (BM_VaVoice_* measures ~4 us per block for the same
// shape), and 56 of them put each channel job past 200 us.
//
// Two shapes, because the render pool's unit of parallelism is the child
// channel and the two shapes stress opposite sides of that:
//
//   BM_Engine_RenderHeavy_Channels/workers:W   8 channels x 56 voices
//       The shape channel fan-out can serve: eight independent jobs.
//   BM_Engine_RenderHeavy_Swarm/workers:W      1 channel x 448 voices
//       The same total work as one job. A channel-granular pool cannot split
//       it; #860's voice slices exist to.
//
// W is the render worker count, set through the internal hook
// INTERNAL::Global().setRenderWorkerCount() for the duration of the run and
// restored to the auto-sized default afterwards: 0 renders everything on the
// calling thread (the serial reference), 2 is today's auto-sized default
// (MAX_AUTO_RENDER_THREADS), and 8 / 24 cover the #647-protocol pool sweep
// that #858's park-and-wake idle strategy is judged by. The `per_channel_job`
// counter is the wall time per block divided by the scene's channel count —
// at W = 0 that is the cost
// of one channel job, which is the figure #856 judges handoff designs against.
// Real time is used throughout: the work runs on pool threads the main
// thread's CPU clock does not see. Baselines are recorded in
// Tests/TEST_PLAN.md.
//
// Scene lifetime: each run builds its channels and sounds, pumps until every
// voice is sounding, times, then destroys them and drains the managers, so no
// benchmark that runs later renders (or even iterates) this scene. Only the
// voice objects are process-lifetime statics: a dspSourceObject must outlive
// its sound's slow-pool delete. The 100-sound scene from bench_mixing.cpp, if
// an earlier benchmark created it, keeps playing underneath — ~1.5 us per block,
// noise against these scenes.

#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "dsp/dspObject.hpp"
#include "dsp/ladderFilter.hpp"
#include "dsp/oscillators.hpp"
#include "internal/global.h"
#include "sound/soundInterface.hpp"

#include "support/bench_helpers.hpp"

#include <benchmark/benchmark.h>

#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

  constexpr int kHeavyChannels = 8;
  constexpr int kVoicesPerChannel = 56;
  constexpr int kHeavyVoices = kHeavyChannels * kVoicesPerChannel; // 448
  constexpr int kHeavyBlocksPerIter = 16;

  // One synth-voice's worth of DSP per block: saw -> swept ladder -> envelope.
  class HeavyVoice : public YSE::DSP::dspSourceObject {
  public:
    HeavyVoice() = default;

    void setup(int index) {
      // Spread the voices over three octaves and give each its own sweep rate
      // and phase so no two voices are the same signal.
      pitch = 55.f * std::pow(2.f, static_cast<float>(index % 36) / 12.f);
      sweepRate = 0.013f + 0.0007f * static_cast<float>(index % 17);
      sweepPhase = 0.37f * static_cast<float>(index);
      filter.setResonance(0.6f);
    }

    void process(YSE::SOUND_STATUS& intent) override {
      if (intent == YSE::SS_WANTSTOSTOP || intent == YSE::SS_WANTSTOPAUSE) {
        intent = intent == YSE::SS_WANTSTOSTOP ? YSE::SS_STOPPED : YSE::SS_PAUSED;
        env = 0.f;
        return;
      }
      intent = YSE::SS_PLAYING;

      samples[0] = osc(pitch);
      sweepPhase += sweepRate;
      filter.setCutoff(900.f + 700.f * std::sin(sweepPhase));
      filter(samples[0]);

      float* p = samples[0].getPtr();
      const UInt n = samples[0].getLength();
      for (UInt i = 0; i < n; ++i) {
        env += (0.05f - env) * 0.002f;
        p[i] *= env;
      }
    }

    void frequency(Flt value) override {
      pitch = value;
    }

  private:
    YSE::DSP::saw osc;
    YSE::DSP::ladderFilter filter;
    float pitch = 110.f;
    float sweepRate = 0.01f;
    float sweepPhase = 0.f;
    float env = 0.f;
  };

  std::array<HeavyVoice, kHeavyVoices>& heavyVoices() {
    static std::array<HeavyVoice, kHeavyVoices> voices;
    static bool initialised = false;
    if (!initialised) {
      for (int v = 0; v < kHeavyVoices; ++v)
        voices[v].setup(v);
      initialised = true;
    }
    return voices;
  }

  // One run's scene: `channelCount` channels under the master, the voices dealt
  // out round-robin. Each sound is head-relative with doppler off so nothing
  // but the voice DSP varies per block.
  struct HeavyScene {
    std::vector<std::unique_ptr<YSE::channel>> channels;
    std::vector<std::unique_ptr<YSE::sound>> sounds;

    explicit HeavyScene(int channelCount) {
      auto& voices = heavyVoices();
      for (int c = 0; c < channelCount; ++c) {
        auto ch = std::make_unique<YSE::channel>();
        ch->create(("bench.heavy" + std::to_string(c)).c_str(), YSE::ChannelMaster());
        channels.push_back(std::move(ch));
      }
      sounds.reserve(kHeavyVoices);
      for (int v = 0; v < kHeavyVoices; ++v) {
        auto s = std::make_unique<YSE::sound>();
        s->create(voices[v], channels[v % channelCount].get(), 1.0f);
        s->relative(true);
        s->doppler(false);
        s->play();
        sounds.push_back(std::move(s));
      }
    }

    bool allPlaying() const {
      for (const auto& s : sounds)
        if (!s->isPlaying()) return false;
      return true;
    }
  };

  void runHeavyScene(benchmark::State& state, int channelCount) {
    if (!BenchHelpers::engineInitOffline()) {
      state.SkipWithError("YSE::System().initOffline() failed");
      return;
    }
    YSE::INTERNAL::Global().setRenderWorkerCount(static_cast<Int>(state.range(0)));
    // Every voice must render, so lift the virtual-sound limit above the voice
    // count for the run: the engine's default of 50 would virtualise most of
    // them. channel::setVirtual(false) is the intended switch for that, but it
    // cannot currently turn virtualisation off (issue #864).
    const int previousMaxSounds = YSE::System().maxSounds();
    YSE::System().maxSounds(kHeavyVoices * 4);

    {
      HeavyScene scene(channelCount);
      if (!BenchHelpers::pumpUntil([&scene] { return scene.allPlaying(); })) {
        state.SkipWithError("heavy scene did not reach a playing state");
      } else {
        // Every voice is sounding; let the envelopes, sound faders and channel
        // volume ramps settle before timing.
        YSE::System().renderOffline(64);
        BenchHelpers::settleControlPlane();
        for (auto _ : state) {
          YSE::System().renderOffline(kHeavyBlocksPerIter);
          benchmark::ClobberMemory();
        }
        const auto blocks = static_cast<int64_t>(state.iterations()) * kHeavyBlocksPerIter;
        state.SetItemsProcessed(blocks * YSE::STANDARD_BUFFERSIZE);
        state.counters["per_block"] = benchmark::Counter(
            kHeavyBlocksPerIter,
            benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
        state.counters["per_channel_job"] = benchmark::Counter(
            static_cast<double>(kHeavyBlocksPerIter) * channelCount,
            benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
      }
    }
    // Drain the destroyed sounds and channels out of the mix tree before the
    // next benchmark (and before the voices are reused by the next run).
    for (int i = 0; i < 16; ++i) {
      YSE::System().update();
      YSE::System().renderOffline(1);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    YSE::System().maxSounds(previousMaxSounds);
    YSE::INTERNAL::Global().setRenderWorkerCount(-1);
  }

  void BM_Engine_RenderHeavy_Channels(benchmark::State& state) {
    runHeavyScene(state, kHeavyChannels);
  }
  BENCHMARK(BM_Engine_RenderHeavy_Channels)
      ->ArgName("workers")
      ->Arg(0)
      ->Arg(1)
      ->Arg(2)
      ->Arg(4)
      ->Arg(8)
      ->Arg(24)
      ->UseRealTime()
      ->Unit(benchmark::kMillisecond);

  void BM_Engine_RenderHeavy_Swarm(benchmark::State& state) {
    runHeavyScene(state, 1);
  }
  BENCHMARK(BM_Engine_RenderHeavy_Swarm)
      ->ArgName("workers")
      ->Arg(0)
      ->Arg(1)
      ->Arg(2)
      ->Arg(4)
      ->Arg(8)
      ->Arg(24)
      ->UseRealTime()
      ->Unit(benchmark::kMillisecond);

} // namespace
