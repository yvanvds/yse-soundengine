// Microbenchmarks for the DX7-class 6-operator FM voice (issue #176):
//   - a pure single-carrier sine patch (algorithm 32, cheapest FM case),
//   - a 2-operator FM patch (one modulator, sidebands), and
//   - a full six-operator brass patch (worst-case per-voice cost),
// each next to the reference sineVoice so the FM core's per-voice cost is
// tracked relative to the cheapest voice (issue #181 acceptance: "FM per-voice").
// A regression in the ported MSFA inner loop (operator envelopes, LFO, the
// fixed-point oscillator bank) is then visible against a stable baseline.

#include "dsp/buffer.hpp"
#include "dsp/fm/fmPatch.hpp"
#include "dsp/fm/fmVoice.hpp"
#include "headers/constants.hpp"
#include "headers/enums.hpp"
#include "synth/sineVoice.hpp"

#include <benchmark/benchmark.h>

namespace {

  // Drive one FM voice through a full block after warming it to sustain, the
  // same shape as bench_va_voice / bench_sampler_voice so the per-voice numbers
  // sit side by side.
  void runFm(benchmark::State& state, const YSE::SYNTH::fmPatch& patch) {
    YSE::SYNTH::fmVoice v;
    v.setPatch(patch); // applied on the next note-on
    v.frequency(60.0f);
    v.velocity(0.9f);

    YSE::SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 8; ++i)
      v.process(intent); // reach sustain (patch is baked at key-down)

    for (auto _ : state) {
      v.process(intent); // one STANDARD_BUFFERSIZE block, PLAYING
      benchmark::DoNotOptimize(v.samples[0].getPtr());
    }
    state.SetItemsProcessed(state.iterations() * YSE::STANDARD_BUFFERSIZE);
  }

  void BM_FmVoice_Sine(benchmark::State& s) {
    runFm(s, YSE::SYNTH::fmPatch::sine());
  }
  BENCHMARK(BM_FmVoice_Sine);

  void BM_FmVoice_2Op(benchmark::State& s) {
    runFm(s, YSE::SYNTH::fmPatch::fm2op());
  }
  BENCHMARK(BM_FmVoice_2Op);

  void BM_FmVoice_Brass6Op(benchmark::State& s) {
    runFm(s, YSE::SYNTH::fmPatch::brass());
  }
  BENCHMARK(BM_FmVoice_Brass6Op);

  // Reference baseline: the cheapest voice, same block, same warm-up.
  void BM_SineVoice_FmReference(benchmark::State& state) {
    YSE::SYNTH::sineVoice v;
    v.frequency(60.0f);
    v.velocity(0.9f);
    YSE::SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 8; ++i)
      v.process(intent);
    for (auto _ : state) {
      v.process(intent);
      benchmark::DoNotOptimize(v.samples[0].getPtr());
    }
    state.SetItemsProcessed(state.iterations() * YSE::STANDARD_BUFFERSIZE);
  }
  BENCHMARK(BM_SineVoice_FmReference);

} // namespace
