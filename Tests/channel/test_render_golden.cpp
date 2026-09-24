// Render golden test: the mix must be bit-identical for any render worker
// count (issue #857, the correctness oracle of the render-scheduler epic #856).
//
// The render pool is being replaced step by step — park-and-wake workers
// (#858), a task-graph scheduler with worker-owned queues and stealing (#859),
// voice slices with private accumulation (#860). Every one of those changes who
// renders which part of a block and in what order, and the ways that goes wrong
// — a task lost or run twice, a sum taken before its inputs finished, two
// threads accumulating into one buffer — all change the output. None of them
// needs to crash to be wrong. So the oracle is the output itself: render one
// deterministic scene with 0, 1, 2 and N render workers and require every
// block to match the 0-worker (fully serial) render bit for bit.
//
// THE SCENE
//
//   master
//   └── golden.parent            2 voices, post-fader send -> golden.capture
//       ├── golden.leaf0..3      3 voices each
//       └── golden.leaf4         3 voices
//           └── golden.deep      2 voices
//   golden.capture (return)      insert = CaptureInsert
//
// Every render worker the pool has gets channel jobs to fight over (five
// siblings under the parent, one of them with a child of its own, so jobs are
// dispatched from worker threads as well as from the rendering thread), and
// the whole subtree reaches the capture through the serial summation walk:
// each child adds into its parent in buffersToParent(), the parent's post-fader
// send accumulates into the return, and the return's insert — itself a pool
// job — records the result. The capture therefore sees exactly what the
// subtree contributes to the master, without the rest of the master mix, which
// in the shared unit-test process carries whatever earlier suites left behind.
//
// Each voice is a naive sawtooth into a ladder filter whose cutoff steps every
// block: pure functions of the voice's own state, which the test resets from
// this thread before each run (between renderOffline() calls nothing renders,
// and the pool's handoff orders the writes before the workers' reads). The
// engine-side state around the voices — fader, pan gains, channel and send
// ramps — is settled before the first run and constant afterwards, and no
// control tick runs during a capture. Sounds are head-relative with doppler
// off, so neither the listener nor the tick clock can reach the mix.
//
// WHERE IT RUNS
//
// Suite `rendergolden` is part of the monolithic yse_unit_tests entry, and so
// of the dev-push TSan sweep (tests-tsan-full), and has its own ctest entry,
// which the per-PR TSan gate runs. It needs the test thread to be the only
// renderer: a live PortAudio stream would render the same scene concurrently
// (and changing the worker count under a live callback drops its jobs), so in
// a process where an earlier suite started one the case skips with a message.
// That only happens on a machine with an audio device; headless CI never has
// one.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "dsp/dspObject.hpp"
#include "dsp/ladderFilter.hpp"
#include "internal/global.h"
#include "sound/soundInterface.hpp"
#include "support/null_device.hpp"
#include "support/timer_pacing.hpp"

namespace {

  constexpr int kLeaves = 5;
  constexpr int kVoicesPerLeaf = 3;
  constexpr int kParentVoices = 2;
  constexpr int kDeepVoices = 2;
  constexpr int kVoices = kParentVoices + kLeaves * kVoicesPerLeaf + kDeepVoices; // 19
  constexpr int kBlocks = 48; // captured blocks per run

  // A deterministic voice: naive saw -> ladder filter with a stepped cutoff.
  // Everything it outputs is a function of state reset() puts back.
  class GoldenVoice : public YSE::DSP::dspSourceObject {
  public:
    void configure(int index) {
      increment = (55.f * static_cast<float>(1 + index % 7) * (1.f + 0.013f * index)) /
                  static_cast<float>(YSE::SAMPLERATE);
      baseCutoff = 400.f + 90.f * static_cast<float>(index);
      filter.setResonance(0.3f + 0.03f * static_cast<float>(index % 5));
    }

    // Test thread, only while nothing renders.
    void reset() {
      phase = 0.f;
      step = 0;
      calls = 0;
      filter.setCutoff(baseCutoff);
      filter.reset();
    }

    void process(YSE::SOUND_STATUS& intent) override {
      if (intent == YSE::SS_WANTSTOSTOP || intent == YSE::SS_WANTSTOPAUSE) {
        intent = intent == YSE::SS_WANTSTOSTOP ? YSE::SS_STOPPED : YSE::SS_PAUSED;
        return;
      }
      intent = YSE::SS_PLAYING;

      float* p = samples[0].getPtr();
      const UInt n = samples[0].getLength();
      for (UInt i = 0; i < n; ++i) {
        p[i] = 0.2f * (2.f * phase - 1.f);
        phase += increment;
        if (phase >= 1.f) phase -= 1.f;
      }
      filter.setCutoff(baseCutoff * (1.f + 0.25f * static_cast<float>(step % 8)));
      filter(samples[0]);
      ++step;
      ++calls;
    }

    void frequency(Flt) override {}

    // Blocks rendered since the last reset(). Read by the test thread after
    // renderOffline() returns, which joins every job of the block.
    int renderedBlocks() const {
      return calls;
    }

  private:
    YSE::DSP::ladderFilter filter;
    float increment = 0.f;
    float phase = 0.f;
    float baseCutoff = 1000.f;
    unsigned step = 0;
    int calls = 0;
  };

  // Records the return bus's summed input, block by block, into storage sized
  // before the run — no allocation on the render path.
  class CaptureInsert : public YSE::DSP::dspObject {
  public:
    void create() override {}

    // Test thread, only while nothing renders.
    void arm(std::size_t channels) {
      width = channels;
      data.assign(static_cast<std::size_t>(kBlocks) * channels * YSE::STANDARD_BUFFERSIZE, 0.f);
      block = 0;
      armed = true;
    }
    void disarm() {
      armed = false;
    }

    void process(MULTICHANNELBUFFER& buffer) override {
      createIfNeeded();
      if (!armed || block >= kBlocks) return;
      for (std::size_t c = 0; c < width && c < buffer.size(); ++c) {
        const UInt n = std::min<UInt>(buffer[c].getLength(), YSE::STANDARD_BUFFERSIZE);
        std::memcpy(slot(block, c), buffer[c].getPtr(), n * sizeof(float));
      }
      ++block;
    }

    int capturedBlocks() const {
      return block;
    }
    const std::vector<float>& samples() const {
      return data;
    }

  private:
    float* slot(int b, std::size_t c) {
      return data.data() + (static_cast<std::size_t>(b) * width + c) * YSE::STANDARD_BUFFERSIZE;
    }

    std::vector<float> data;
    std::size_t width = 0;
    int block = 0;
    bool armed = false;
  };

  // File scope: a dspSourceObject / dspObject must outlive its sound's (or
  // return's) slow-pool teardown, which completes after the case returns.
  std::array<GoldenVoice, kVoices> g_voices;
  CaptureInsert g_capture;

  void pump(int iterations) {
    for (int i = 0; i < iterations; ++i) {
      YSE::System().update();
      YSE::System().renderOffline(1);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  struct RunResult {
    std::vector<float> samples;
    int captured = 0;
    int minVoiceBlocks = 0;
    int maxVoiceBlocks = 0;
  };

  // Render kBlocks with `workers` render workers from freshly reset voices.
  RunResult renderWith(int workers, std::size_t width) {
    YSE::INTERNAL::Global().setRenderWorkerCount(workers);
    for (auto& v : g_voices)
      v.reset();
    g_capture.arm(width);
    for (int b = 0; b < kBlocks; ++b)
      YSE::System().renderOffline(1);
    g_capture.disarm();

    RunResult r;
    r.samples = g_capture.samples();
    r.captured = g_capture.capturedBlocks();
    r.minVoiceBlocks = kBlocks * 2;
    r.maxVoiceBlocks = 0;
    for (const auto& v : g_voices) {
      r.minVoiceBlocks = std::min(r.minVoiceBlocks, v.renderedBlocks());
      r.maxVoiceBlocks = std::max(r.maxVoiceBlocks, v.renderedBlocks());
    }
    return r;
  }

  // Index of the first sample whose bit pattern differs, or -1.
  long firstDifference(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
      std::uint32_t x = 0;
      std::uint32_t y = 0;
      std::memcpy(&x, &a[i], sizeof x);
      std::memcpy(&y, &b[i], sizeof y);
      if (x != y) return static_cast<long>(i);
    }
    return -1;
  }

} // namespace

TEST_SUITE("rendergolden") {

  TEST_CASE("rendergolden: the mix is bit-identical for 0, 1, 2 and N render workers (#857)") {
    if (!TestHelpers::engineInit()) return;
    if (YSE::System().getActiveSampleRate() != 0.0) {
      MESSAGE("skipped: an audio stream is live in this process, so the test thread is not the "
              "only renderer. The golden render needs to be the sole driver of the mix; it runs "
              "in its own process (yse_tests_rendergolden) and on headless CI.");
      return;
    }

    // Every voice must render regardless of what else is alive in the
    // process: lift the virtual-sound limit for the case, the same way the
    // #857 heavy-voice benchmarks do.
    const int previousMaxSounds = YSE::System().maxSounds();
    YSE::System().maxSounds(4096);

    for (int v = 0; v < kVoices; ++v)
      g_voices[v].configure(v);

    YSE::channel capture;
    capture.makeReturn("golden.capture");
    capture.setDSP(&g_capture);

    YSE::channel parent;
    parent.create("golden.parent", YSE::ChannelMaster());
    std::array<YSE::channel, kLeaves> leaves;
    for (int l = 0; l < kLeaves; ++l)
      leaves[l].create(("golden.leaf" + std::to_string(l)).c_str(), parent);
    YSE::channel deep;
    deep.create("golden.deep", leaves[kLeaves - 1]);
    pump(8); // channels live before sounds attach to them

    std::vector<std::unique_ptr<YSE::sound>> sounds;
    auto attach = [&sounds](GoldenVoice& voice, YSE::channel& ch) {
      auto s = std::make_unique<YSE::sound>();
      s->create(voice, &ch, 0.8f);
      s->relative(true);
      s->doppler(false);
      s->play();
      sounds.push_back(std::move(s));
    };
    int next = 0;
    for (int i = 0; i < kParentVoices; ++i)
      attach(g_voices[next++], parent);
    for (int l = 0; l < kLeaves; ++l)
      for (int i = 0; i < kVoicesPerLeaf; ++i)
        attach(g_voices[next++], leaves[l]);
    for (int i = 0; i < kDeepVoices; ++i)
      attach(g_voices[next++], deep);
    REQUIRE(next == kVoices);

    parent.send(0, capture, 1.0f);

    const bool allPlaying = TestHelpers::pacedPump(
        3000,
        [&sounds] {
          for (const auto& s : sounds)
            if (!s->isPlaying()) return false;
          return true;
        },
        [] {
          YSE::System().update();
          YSE::System().renderOffline(1);
        },
        2);
    REQUIRE(allPlaying);

    // Settle every engine-side ramp (sound faders, pan gains, channel volumes,
    // the send level), then leave no control work pending: a manager update
    // inside a captured run would be the one thing in the render path not
    // driven by the voices.
    pump(4);
    YSE::System().renderOffline(64);
    while (YSE::INTERNAL::Global().needsUpdate())
      YSE::INTERNAL::Global().updateDone();

    const std::size_t width = static_cast<std::size_t>(capture.getNumOutputs());
    REQUIRE(width > 0);

    const int n = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 3, 8);
    const RunResult reference = renderWith(0, width);

    // The reference must be a real mix of every voice, or matching it proves
    // nothing: every block captured, every voice rendered once per block, and
    // audible signal in the capture.
    CHECK(reference.captured == kBlocks);
    CHECK(reference.minVoiceBlocks == kBlocks);
    CHECK(reference.maxVoiceBlocks == kBlocks);
    float peak = 0.f;
    for (float s : reference.samples)
      peak = std::max(peak, std::fabs(s));
    CHECK(peak > 0.05f);

    // The worker counts under test, ending on 0 again: the serial render must
    // also reproduce itself after the pool has been cycled.
    const int counts[] = {1, 2, n, 0};
    for (int workers : counts) {
      const RunResult run = renderWith(workers, width);
      const long diff = firstDifference(reference.samples, run.samples);
      INFO("render workers: " << workers);
      INFO("first differing sample: "
           << diff << " (block "
           << (diff < 0 ? -1 : diff / static_cast<long>(width * YSE::STANDARD_BUFFERSIZE)) << ")");
      CHECK(run.captured == kBlocks);
      CHECK(run.minVoiceBlocks == kBlocks); // no voice skipped...
      CHECK(run.maxVoiceBlocks == kBlocks); // ...and none rendered twice
      CHECK(diff == -1);
    }

    // Tear down: auto-sized pool and the process's virtual-sound limit back,
    // every source stopped, the capture detached before the return goes away.
    YSE::INTERNAL::Global().setRenderWorkerCount(-1);
    YSE::System().maxSounds(previousMaxSounds);
    for (auto& s : sounds)
      s->stop();
    parent.clearSend(0);
    capture.setDSP(nullptr);
    pump(8);
    sounds.clear();
    pump(8);
  }

} // TEST_SUITE("rendergolden")
