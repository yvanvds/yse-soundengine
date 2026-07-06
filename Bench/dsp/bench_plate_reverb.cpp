// Tier 1 microbenchmark for the Dattorro plate reverb module (issue #162).
//
// The plate reverb is one of the expensive DSP modules — a four-allpass input
// diffuser plus a cross-coupled figure-eight tank of eight delay/allpass lines
// and two damping filters, all running per sample. This benchmark measures the
// steady-state per-block cost of process() so regressions in the tank inner
// loop are caught, and gives a direct CPU comparison point against the existing
// Freeverb-derived INTERNAL::reverbDSP (whose own process() has no public entry
// point — see bench_reverb.cpp for that config-path benchmark).
//
// No engine init is needed: SAMPLERATE has its static-init default (44100) from
// the portaudioDeviceManager TU, which is all the module's create() reads.

#include "dsp/buffer.hpp"
#include "dsp/modules/plateReverb.hpp"
#include "headers/defines.hpp"

#include <benchmark/benchmark.h>

namespace {

  constexpr unsigned int kBlock = 1024;

} // namespace

// ── stereo process (the send-return / insert use case) ──────────────────────

static void BM_PlateReverb_ProcessStereo(benchmark::State& state) {
  YSE::DSP::MODULES::plateReverb rev;
  rev.decay(0.8f).damping(6000.0f).preDelay(20.0f).impact(0.5f);

  MULTICHANNELBUFFER buf(2);
  buf[0].resize(kBlock);
  buf[1].resize(kBlock);
  buf[0] = 0.25f;
  buf[1] = 0.25f;

  // Warm up so create()/sizing and the tank fill happen outside the timed loop.
  for (int i = 0; i < 8; ++i)
    rev.process(buf);

  for (auto _ : state) {
    buf[0] = 0.25f;
    buf[1] = 0.25f;
    rev.process(buf);
    benchmark::DoNotOptimize(buf[0].getPtr());
    benchmark::DoNotOptimize(buf[1].getPtr());
  }
  state.SetItemsProcessed(state.iterations() * kBlock * 2);
}
BENCHMARK(BM_PlateReverb_ProcessStereo);

// ── mono process (downmix-to-tank, single-channel distribution) ─────────────

static void BM_PlateReverb_ProcessMono(benchmark::State& state) {
  YSE::DSP::MODULES::plateReverb rev;
  rev.decay(0.8f).damping(6000.0f).impact(0.5f);

  MULTICHANNELBUFFER buf(1);
  buf[0].resize(kBlock);
  buf[0] = 0.25f;

  for (int i = 0; i < 8; ++i)
    rev.process(buf);

  for (auto _ : state) {
    buf[0] = 0.25f;
    rev.process(buf);
    benchmark::DoNotOptimize(buf[0].getPtr());
  }
  state.SetItemsProcessed(state.iterations() * kBlock);
}
BENCHMARK(BM_PlateReverb_ProcessMono);
