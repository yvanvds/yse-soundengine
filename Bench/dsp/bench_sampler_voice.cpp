// Microbenchmarks for the SFZ sampler voice (issue #174):
//   - a single-layer sampler render block (looping, pitch-shifted), and
//   - a 4-layer render block (worst-case per-voice cost),
// each next to the reference sineVoice so the sampler's per-voice cost is
// tracked relative to the cheapest voice (acceptance: "sampler voice vs sine
// voice cost"). A regression in the cubic read / loop / envelope inner loop is
// then visible against a stable baseline.

#include "dsp/buffer.hpp"
#include "headers/constants.hpp"
#include "headers/enums.hpp"
#include "synth/samplerVoice.hpp"
#include "synth/sineVoice.hpp"

#include <benchmark/benchmark.h>

#include <cmath>
#include <memory>

namespace {

  // Build an N-layer sampler instrument: N overlapping full-range regions each
  // playing a resident, looping sine so the render path exercises the cubic
  // read + loop wrap + amp EG for every layer.
  std::shared_ptr<YSE::SYNTH::samplerInstrument> makeInst(int layers) {
    auto inst = std::make_shared<YSE::SYNTH::samplerInstrument>();
    const long frames = 4096;
    const float sr = static_cast<float>(YSE::SAMPLERATE);
    for (int k = 0; k < layers; ++k) {
      YSE::SYNTH::residentSample rs;
      YSE::DSP::fileBuffer buf;
      buf.resize(static_cast<UInt>(frames));
      float* p = buf.getPtr();
      for (long i = 0; i < frames; ++i)
        p[i] = std::sin(2.0f * 3.14159265358979f * 220.0f * static_cast<float>(i) / sr);
      rs.frames = frames;
      rs.loaded = true;
      rs.channels.push_back(buf);
      inst->samples.push_back(std::move(rs));

      YSE::DSP::sfzRegion r;
      r.sampleIndex = k;
      r.pitchKeycenter = 60;
      r.egSustain = 1.0f;
      r.ampVeltrack = 0.0f;
      r.loopMode = YSE::DSP::SFZ_LOOP_CONTINUOUS;
      r.loopStart = 0;
      r.loopEnd = frames - 1;
      inst->model.regions.push_back(r);
    }
    inst->model.valid = true;
    return inst;
  }

  void runSampler(benchmark::State& state, int layers) {
    YSE::SYNTH::samplerVoice v;
    v.setInstrument(makeInst(layers));
    v.frequency(64.0f); // pitch-shifted a few semitones off the keycenter
    v.velocity(0.9f);

    YSE::SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 8; ++i)
      v.process(intent);

    for (auto _ : state) {
      v.process(intent); // one STANDARD_BUFFERSIZE block, PLAYING
      benchmark::DoNotOptimize(v.samples[0].getPtr());
    }
    state.SetItemsProcessed(state.iterations() * YSE::STANDARD_BUFFERSIZE);
  }

  void BM_SamplerVoice_1Layer(benchmark::State& s) {
    runSampler(s, 1);
  }
  BENCHMARK(BM_SamplerVoice_1Layer);

  void BM_SamplerVoice_4Layer(benchmark::State& s) {
    runSampler(s, 4);
  }
  BENCHMARK(BM_SamplerVoice_4Layer);

  // Reference baseline: the cheapest voice, same block, same warm-up.
  void BM_SineVoice_Reference(benchmark::State& state) {
    YSE::SYNTH::sineVoice v;
    v.frequency(64.0f);
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
  BENCHMARK(BM_SineVoice_Reference);

} // namespace
