// Tests for YSE::SYNTH::fmVoice (issue #176) — the DX7-class 6-operator FM
// voice ported from music-synthesizer-for-android, and its fmPatch contract.
//
// Coverage:
//   - intent-driven lifecycle (WANTSTOPLAY -> PLAYING -> release -> STOPPED),
//   - the built-in sine patch renders a clean tone at the requested pitch
//     (FFT peak-bin),
//   - the 2-operator FM patch generates sidebands the sine patch does not
//     (carrier + sideband spectrum),
//   - all 32 algorithms instantiate and render finite, non-silent audio
//     (algorithm routing + feedback stability),
//   - clone() shares the patch but keeps independent per-voice state,
//   - process() does not allocate after warm-up,
//   - fmPatch::toUnpacked lays fields out at the byte offsets #177 depends on.
//
// No audio device required; SAMPLERATE is initialised by the
// portaudioDeviceManager translation unit at static-initialisation time (44100
// by default; CI also forces 48000). Tones and windows are derived from the
// actual SAMPLERATE so the tests hold at any rate.

#include <doctest/doctest.h>
#include <cmath>
#include <vector>

#include "dsp/fm/fmVoice.hpp"
#include "dsp/fm/fmPatch.hpp"
#include "dsp/fourier/fft.hpp"
#include "dsp/math.hpp"
#include "support/audio_helpers.hpp"
#include "support/alloc_probe.hpp"

TEST_SUITE("dsp") {

  using YSE::SOUND_STATUS;
  using YSE::SYNTH::fmPatch;
  using YSE::SYNTH::fmVoice;

  // Drive a voice to note-on, let it settle, then capture N samples of the
  // sustaining tone in STANDARD_BUFFERSIZE blocks.
  static unsigned captureSustain(fmVoice & v, YSE::DSP::buffer & out, unsigned N, int warm = 8) {
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < warm; ++i)
      v.process(intent);
    const unsigned block = YSE::STANDARD_BUFFERSIZE;
    unsigned filled = 0;
    while (filled + block <= N) {
      v.process(intent); // stays PLAYING
      out.copyFrom(v.samples[0], 0, filled, block);
      filled += block;
    }
    return filled;
  }

  // Summed magnitude-squared energy in a small window around `hz`.
  static float binEnergy(float* re, float* im, unsigned N, float hz) {
    const float binHz = static_cast<float>(YSE::SAMPLERATE) / static_cast<float>(N);
    int bin = static_cast<int>(std::lround(hz / binHz));
    float e = 0.f;
    for (int k = bin - 1; k <= bin + 1; ++k) {
      if (k < 1 || k > static_cast<int>(N / 2)) continue;
      e += re[k] * re[k] + im[k] * im[k];
    }
    return e;
  }

  // ─── lifecycle ──────────────────────────────────────────────────────────────

  TEST_CASE("fmVoice: WANTSTOPLAY settles the intent to PLAYING and sounds") {
    fmVoice v;
    v.frequency(69.f);
    v.velocity(1.f);
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    CHECK(intent == YSE::SS_PLAYING);
    for (int i = 0; i < 20; ++i)
      v.process(intent);
    CHECK(TestHelpers::measureRms(v.samples[0]) > 0.005f);
  }

  TEST_CASE("fmVoice: WANTSTOSTOP releases and settles to STOPPED") {
    fmVoice v;
    v.frequency(69.f);
    v.velocity(1.f);

    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 20; ++i)
      v.process(intent);

    intent = YSE::SS_WANTSTOSTOP;
    int blocks = 0;
    const int kMax = 2000;
    while (intent != YSE::SS_STOPPED && blocks < kMax) {
      v.process(intent);
      ++blocks;
    }
    CHECK(intent == YSE::SS_STOPPED);
    CHECK(blocks < kMax);
  }

  TEST_CASE("fmVoice: released before it ever attacked settles immediately") {
    fmVoice v;
    v.frequency(60.f);
    v.velocity(1.f);
    SOUND_STATUS intent = YSE::SS_WANTSTOSTOP; // note-off with no prior note-on
    v.process(intent);
    CHECK(intent == YSE::SS_STOPPED);
  }

  // ─── pitch (sine patch) ───────────────────────────────────────────────────

  TEST_CASE("fmVoice: sine patch renders a tone at the requested pitch") {
    fmVoice v; // constructs with the sine patch by default
    const float note = 69.f; // A4
    v.frequency(note);
    v.velocity(1.f);
    const float noteHz = YSE::DSP::MidiToFreq(note);

    const unsigned N = 2048;
    YSE::DSP::buffer re(N), im(N);
    re = 0.f;
    im = 0.f;
    unsigned filled = captureSustain(v, re, N);
    REQUIRE(filled == N);
    CHECK(TestHelpers::measureRms(re) > 0.005f);

    YSE::DSP::fft f;
    f(re, im);
    unsigned peak = TestHelpers::peakBinIndex(f.getReal().getPtr(), f.getImaginary().getPtr(), N);
    const float binHz = static_cast<float>(YSE::SAMPLERATE) / static_cast<float>(N);
    const float peakHz = static_cast<float>(peak) * binHz;
    CHECK(std::abs(peakHz - noteHz) < 3.f * binHz);
  }

  // ─── FM sidebands (carrier + sideband structure) ─────────────────────────

  TEST_CASE("fmVoice: 2-operator FM patch generates sidebands the sine patch lacks") {
    const float note = 57.f; // A3, ~220 Hz — fundamental + 2nd well resolved
    const float noteHz = YSE::DSP::MidiToFreq(note);
    const unsigned N = 4096;

    // Sine reference: essentially no energy at 2f.
    fmVoice sine;
    sine.setPatch(fmPatch::sine());
    sine.frequency(note);
    sine.velocity(1.f);
    YSE::DSP::buffer sre(N), sim(N);
    sre = 0.f;
    sim = 0.f;
    REQUIRE(captureSustain(sine, sre, N) == N);
    YSE::DSP::fft sf;
    sf(sre, sim);
    float sineFund = binEnergy(sf.getReal().getPtr(), sf.getImaginary().getPtr(), N, noteHz);
    float sineHarm = binEnergy(sf.getReal().getPtr(), sf.getImaginary().getPtr(), N, 2.f * noteHz);

    // 2-op FM: carrier plus real sideband energy at 2f.
    fmVoice fm;
    fm.setPatch(fmPatch::fm2op());
    fm.frequency(note);
    fm.velocity(1.f);
    YSE::DSP::buffer fre(N), fim(N);
    fre = 0.f;
    fim = 0.f;
    REQUIRE(captureSustain(fm, fre, N) == N);
    CHECK(TestHelpers::measureRms(fre) > 0.005f);
    YSE::DSP::fft ff;
    ff(fre, fim);
    float fmFund = binEnergy(ff.getReal().getPtr(), ff.getImaginary().getPtr(), N, noteHz);
    float fmHarm = binEnergy(ff.getReal().getPtr(), ff.getImaginary().getPtr(), N, 2.f * noteHz);

    // The carrier is present in both.
    CHECK(fmFund > 0.f);
    CHECK(sineFund > 0.f);
    // The FM patch has substantial sideband energy at 2f...
    CHECK(fmHarm > 0.05f * fmFund);
    // ...far more than the near-pure sine reference.
    CHECK(fmHarm > 10.f * sineHarm);
  }

  // ─── all 32 algorithms (routing + feedback stability) ────────────────────

  TEST_CASE("fmVoice: all 32 algorithms instantiate and render finite audio") {
    for (int alg = 0; alg < 32; ++alg) {
      fmPatch p = fmPatch::brass();
      p.algorithm = static_cast<uint8_t>(alg);
      // Ensure every operator can sound so each algorithm's carriers are lit.
      for (int i = 0; i < 6; ++i)
        p.op[i].outputLevel = 99;

      fmVoice v;
      v.setPatch(p);
      v.frequency(60.f);
      v.velocity(1.f);

      const unsigned N = 512;
      YSE::DSP::buffer buf(N);
      buf = 0.f;
      unsigned filled = captureSustain(v, buf, N, /*warm*/ 4);
      REQUIRE(filled == N);

      float* d = buf.getPtr();
      bool allFinite = true;
      for (unsigned i = 0; i < N; ++i)
        if (!std::isfinite(d[i])) allFinite = false;

      INFO("algorithm index = " << alg);
      CHECK(allFinite);
      CHECK(TestHelpers::measureRms(buf) > 0.f);
    }
  }

  // ─── clone semantics ──────────────────────────────────────────────────────

  TEST_CASE("fmVoice: clone shares the patch but keeps independent state") {
    fmVoice proto;
    proto.parameters().feedback = 3;
    std::unique_ptr<YSE::SYNTH::dspVoice> a(proto.clone());
    std::unique_ptr<YSE::SYNTH::dspVoice> b(proto.clone());

    auto* fa = dynamic_cast<fmVoice*>(a.get());
    auto* fb = dynamic_cast<fmVoice*>(b.get());
    REQUIRE(fa != nullptr);
    REQUIRE(fb != nullptr);

    // Same shared patch object.
    CHECK(fa->patch().get() == proto.patch().get());
    CHECK(fb->patch().get() == proto.patch().get());

    // Independent DSP state: play A only; B stays silent.
    fa->frequency(69.f);
    fa->velocity(1.f);
    fb->frequency(69.f);
    fb->velocity(1.f);
    SOUND_STATUS ia = YSE::SS_WANTSTOPLAY;
    for (int i = 0; i < 20; ++i)
      fa->process(ia);
    SOUND_STATUS ib = YSE::SS_STOPPED;
    fb->process(ib);
    CHECK(TestHelpers::measureRms(fa->samples[0]) > 0.005f);
    CHECK(TestHelpers::measureRms(fb->samples[0]) == doctest::Approx(0.f));
  }

  // ─── real-time discipline ─────────────────────────────────────────────────

  TEST_CASE("fmVoice: process() does not allocate after warm-up") {
    fmVoice v;
    v.setPatch(fmPatch::fm2op());
    v.frequency(64.f);
    v.velocity(0.9f);

    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 8; ++i)
      v.process(intent);

    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 64; ++i)
        v.process(intent);
      // Drive a note-off/settle cycle too — no allocation on any control edge.
      SOUND_STATUS off = YSE::SS_WANTSTOSTOP;
      for (int i = 0; i < 8; ++i)
        v.process(off);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // ─── fmPatch contract (#177 byte-layout) ─────────────────────────────────

  TEST_CASE("fmPatch: toUnpacked lays fields at the DX7 156-byte offsets") {
    fmPatch p = fmPatch::sine();
    // Distinct, checkable values across an operator and the global block.
    p.op[0].egRate[0] = 11;
    p.op[0].egLevel[3] = 22;
    p.op[0].detune = 14;
    p.op[2].outputLevel = 77;
    p.op[5].freqCoarse = 5;
    p.pitchEgRate[1] = 33;
    p.pitchEgLevel[3] = 44;
    p.algorithm = 17;
    p.feedback = 5;
    p.lfoWaveform = 4;
    p.pitchModSens = 6;
    p.transpose = 30;
    p.name[0] = 'F';
    p.name[9] = 'M';
    p.opEnabled = 0x3f;

    char u[156];
    p.toUnpacked(u);

    CHECK(u[0 * 21 + 0] == 11); // op0 EG rate 1
    CHECK(u[0 * 21 + 7] == 22); // op0 EG level 4
    CHECK(u[0 * 21 + 20] == 14); // op0 detune
    CHECK(u[2 * 21 + 16] == 77); // op2 output level
    CHECK(u[5 * 21 + 18] == 5); // op5 coarse freq
    CHECK(u[127] == 33); // pitch EG rate 2
    CHECK(u[133] == 44); // pitch EG level 4
    CHECK(u[134] == 17); // algorithm
    CHECK(u[135] == 5); // feedback
    CHECK(u[142] == 4); // LFO waveform
    CHECK(u[143] == 6); // pitch mod sensitivity
    CHECK(u[144] == 30); // transpose
    CHECK(u[145] == 'F'); // name[0]
    CHECK(u[154] == 'M'); // name[9]
    CHECK(static_cast<unsigned char>(u[155]) == 0x3f); // operator on/off
  }

} // TEST_SUITE("dsp")
