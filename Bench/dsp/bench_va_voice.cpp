// Microbenchmarks for the virtual-analog voice path (issue #175):
//   - the standalone Moog-style ladder filter (per-block in-place), and
//   - a full vaVoice render block for the common oscillator/filter modes.
//
// These track the per-voice real-time budget documented in
// docs/design/synth_core.md §11 so a regression in the ladder or the voice's
// per-sample inner loop is visible.

#include "dsp/ladderFilter.hpp"
#include "dsp/buffer.hpp"
#include "synth/vaVoice.hpp"
#include "headers/constants.hpp"
#include "headers/enums.hpp"

#include <benchmark/benchmark.h>

namespace {

  constexpr unsigned int kBlock = 1024;
  constexpr float kSignal = 0.3f;

  // ── ladder filter ───────────────────────────────────────────────────────────

  void BM_LadderFilter(benchmark::State& state) {
    YSE::DSP::buffer buf(kBlock);
    YSE::DSP::ladderFilter f;
    f.setCutoff(1200.0f);
    f.setResonance(0.7f);
    f.reset();
    f.setResonance(0.7f);

    for (auto _ : state) {
      buf = kSignal;
      f(buf);
      benchmark::DoNotOptimize(buf.getPtr());
    }
    state.SetItemsProcessed(state.iterations() * kBlock);
  }
  BENCHMARK(BM_LadderFilter);

  // ── full vaVoice render ─────────────────────────────────────────────────────

  void runVoice(benchmark::State& state, YSE::SYNTH::VA_WAVEFORM wave, bool dualOsc) {
    YSE::SYNTH::vaVoice v;
    YSE::SYNTH::vaParams& p = v.parameters();
    p.oscWave[0].store(wave);
    p.oscLevel[0].store(0.7f);
    if (dualOsc) {
      p.oscWave[1].store(wave);
      p.oscLevel[1].store(0.7f);
      p.oscDetune[1].store(0.1f);
    }
    p.filterEnvAmount.store(2.0f);
    v.frequency(60.0f);
    v.velocity(0.9f);

    YSE::SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 8; ++i)
      v.process(intent); // reach sustain

    for (auto _ : state) {
      v.process(intent); // one STANDARD_BUFFERSIZE block, PLAYING
      benchmark::DoNotOptimize(v.samples[0].getPtr());
    }
    state.SetItemsProcessed(state.iterations() * YSE::STANDARD_BUFFERSIZE);
  }

  void BM_VaVoice_SingleSaw(benchmark::State& s) {
    runVoice(s, YSE::SYNTH::VA_SAW, false);
  }
  BENCHMARK(BM_VaVoice_SingleSaw);

  void BM_VaVoice_DualSaw(benchmark::State& s) {
    runVoice(s, YSE::SYNTH::VA_SAW, true);
  }
  BENCHMARK(BM_VaVoice_DualSaw);

  void BM_VaVoice_Wavetable(benchmark::State& s) {
    runVoice(s, YSE::SYNTH::VA_WAVETABLE, false);
  }
  BENCHMARK(BM_VaVoice_Wavetable);

} // namespace
