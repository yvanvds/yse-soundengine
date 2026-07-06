// Tests for YSE::SYNTH::vaVoice (issue #175) — the virtual-analog + wavetable
// synth voice, and its shared vaParams patch.
//
// Coverage:
//   - intent-driven lifecycle (WANTSTOPLAY -> PLAYING -> release -> STOPPED),
//   - rendered pitch matches the requested note (sine osc, FFT peak-bin),
//   - a detuned dual-saw "Minimoog-ish" patch renders with harmonics,
//   - wavetable morph is click-free (bounded first-difference across a sweep),
//   - an AKWF-length (600-sample) single-cycle table loads and plays,
//   - velocity routes to amplitude,
//   - clone() shares the patch but keeps independent per-voice state,
//   - no heap allocation in process() after warm-up.
//
// No audio device required; SAMPLERATE is initialised to 44100 by the
// portaudioDeviceManager translation unit at static-initialisation time.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <vector>
#include "synth/vaVoice.hpp"
#include "dsp/fourier/fft.hpp"
#include "dsp/math.hpp"
#include "support/audio_helpers.hpp"
#include "support/alloc_probe.hpp"

TEST_SUITE("dsp") {

  using YSE::SOUND_STATUS;
  using YSE::SYNTH::dspVoice;
  using YSE::SYNTH::vaParams;
  using YSE::SYNTH::vaVoice;

  // Render `blocks` blocks of a sustaining voice into a freshly captured
  // N-sample buffer, starting after `warm` settle blocks. Returns the number
  // of samples captured.
  static unsigned captureSustain(vaVoice & v, YSE::DSP::buffer & out, unsigned N, int warm = 8) {
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

  // ─── lifecycle ──────────────────────────────────────────────────────────────

  TEST_CASE("vaVoice: WANTSTOPLAY settles the intent to PLAYING and sounds") {
    vaVoice v;
    v.frequency(69.f);
    v.velocity(1.f);
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    CHECK(intent == YSE::SS_PLAYING);
    for (int i = 0; i < 20; ++i)
      v.process(intent);
    CHECK(TestHelpers::measureRms(v.samples[0]) > 0.02f);
  }

  TEST_CASE("vaVoice: WANTSTOSTOP releases and settles to STOPPED") {
    vaVoice v;
    v.frequency(69.f);
    v.velocity(1.f);
    v.parameters().ampAttack.store(0.005f);
    v.parameters().ampRelease.store(0.03f);

    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 20; ++i)
      v.process(intent);

    intent = YSE::SS_WANTSTOSTOP;
    int blocks = 0;
    const int kMax = 1000;
    while (intent != YSE::SS_STOPPED && blocks < kMax) {
      v.process(intent);
      ++blocks;
    }
    CHECK(intent == YSE::SS_STOPPED);
    CHECK(blocks < kMax);
    v.process(intent);
    CHECK(TestHelpers::decaysToSilence(v.samples[0], 1e-4f));
  }

  TEST_CASE("vaVoice: note-off before attack settles immediately") {
    vaVoice v;
    v.frequency(69.f);
    v.velocity(1.f);
    SOUND_STATUS intent = YSE::SS_WANTSTOSTOP; // released before any play
    v.process(intent);
    CHECK(intent == YSE::SS_STOPPED);
    CHECK(v.samples[0].isSilent());
  }

  // ─── rendered pitch ─────────────────────────────────────────────────────────

  TEST_CASE("vaVoice: rendered pitch matches the requested note (sine osc)") {
    vaVoice v;
    vaParams& p = v.parameters();
    p.oscWave[0].store(YSE::SYNTH::VA_SINE);
    p.oscLevel[0].store(1.f);
    p.cutoff.store(16000.f); // filter wide open
    p.resonance.store(0.f);
    p.filterEnvAmount.store(0.f);
    p.keyTracking.store(0.f);
    p.ampAttack.store(0.001f);
    p.ampDecay.store(0.001f);
    p.ampSustain.store(1.f);

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

    YSE::DSP::fft f;
    f(re, im);
    unsigned peak = TestHelpers::peakBinIndex(f.getReal().getPtr(), f.getImaginary().getPtr(), N);
    const float binHz = static_cast<float>(YSE::SAMPLERATE) / static_cast<float>(N);
    const float peakHz = static_cast<float>(peak) * binHz;
    CHECK(std::abs(peakHz - noteHz) < 2.f * binHz);
  }

  // ─── Minimoog-ish dual-saw patch ────────────────────────────────────────────

  TEST_CASE("vaVoice: detuned dual-saw patch renders with harmonics") {
    vaVoice v;
    vaParams& p = v.parameters();
    p.oscWave[0].store(YSE::SYNTH::VA_SAW);
    p.oscWave[1].store(YSE::SYNTH::VA_SAW);
    p.oscLevel[0].store(0.6f);
    p.oscLevel[1].store(0.6f);
    p.oscDetune[1].store(0.12f); // slight detune → fat unison
    p.cutoff.store(8000.f);
    p.resonance.store(0.2f);
    p.filterEnvAmount.store(0.f);
    p.keyTracking.store(0.f);
    p.ampAttack.store(0.001f);
    p.ampDecay.store(0.001f);
    p.ampSustain.store(1.f);

    const float note = 45.f; // A2, ~110 Hz — fundamental + harmonics resolvable
    v.frequency(note);
    v.velocity(1.f);
    const float noteHz = YSE::DSP::MidiToFreq(note);

    const unsigned N = 4096;
    YSE::DSP::buffer re(N), im(N);
    re = 0.f;
    im = 0.f;
    unsigned filled = captureSustain(v, re, N);
    REQUIRE(filled == N);
    CHECK(TestHelpers::measureRms(re) > 0.02f);

    YSE::DSP::fft f;
    f(re, im);
    float* fr = f.getReal().getPtr();
    float* fi = f.getImaginary().getPtr();
    const float binHz = static_cast<float>(YSE::SAMPLERATE) / static_cast<float>(N);

    auto binEnergy = [&](float hz) {
      int bin = static_cast<int>(hz / binHz + 0.5f);
      float e = 0.f;
      for (int k = bin - 1; k <= bin + 1; ++k) { // small window: leakage + detune
        if (k < 1 || k > static_cast<int>(N / 2)) continue;
        e += fr[k] * fr[k] + fi[k] * fi[k];
      }
      return e;
    };

    float fund = binEnergy(noteHz);
    float h2 = binEnergy(noteHz * 2.f);
    float h3 = binEnergy(noteHz * 3.f);
    CHECK(fund > 0.f);
    // A saw is harmonically rich — clear energy above the fundamental.
    CHECK(h2 > fund * 0.02f);
    CHECK(h3 > 0.f);
  }

  // ─── wavetable morph ────────────────────────────────────────────────────────

  TEST_CASE("vaVoice: wavetable morph is click-free across a sweep") {
    vaVoice v;
    vaParams& p = v.parameters();
    p.oscWave[0].store(YSE::SYNTH::VA_WAVETABLE);
    p.oscLevel[0].store(1.f);
    p.cutoff.store(16000.f);
    p.resonance.store(0.f);
    p.filterEnvAmount.store(0.f);
    p.keyTracking.store(0.f);
    p.ampAttack.store(0.002f);
    p.ampSustain.store(1.f);
    p.wavetablePosition.store(0.f); // start at bank[0] (sine)

    v.frequency(45.f); // low note → small per-sample steps
    v.velocity(1.f);

    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 8; ++i)
      v.process(intent); // settle amp env

    // Sweep the morph position over the smooth sine→triangle region while
    // rendering, and track the largest sample-to-sample jump (including across
    // block boundaries). A morph click would show up as a large discontinuity.
    const int blocks = 120;
    float prev = 0.f;
    bool havePrev = false;
    float maxStep = 0.f;
    for (int b = 0; b < blocks; ++b) {
      float pos = 0.5f * (static_cast<float>(b) / static_cast<float>(blocks - 1));
      p.wavetablePosition.store(pos); // sine (0) → triangle (~0.5)
      v.process(intent);
      float* out = v.samples[0].getPtr();
      for (unsigned i = 0; i < v.samples[0].getLength(); ++i) {
        if (havePrev) {
          float step = std::abs(out[i] - prev);
          if (step > maxStep) maxStep = step;
        }
        prev = out[i];
        havePrev = true;
      }
    }
    // Sine and triangle are continuous; a crossfade between them at a low note
    // keeps successive samples close. A click would be near full-scale.
    CHECK(maxStep < 0.2f);
  }

  TEST_CASE("vaVoice: morph position changes the timbre") {
    auto rmsAt = [](float pos) {
      vaVoice v;
      vaParams& p = v.parameters();
      p.oscWave[0].store(YSE::SYNTH::VA_WAVETABLE);
      p.oscLevel[0].store(1.f);
      p.cutoff.store(16000.f);
      p.resonance.store(0.f);
      p.filterEnvAmount.store(0.f);
      p.keyTracking.store(0.f);
      p.ampSustain.store(1.f);
      p.wavetablePosition.store(pos);
      v.frequency(57.f);
      v.velocity(1.f);
      YSE::DSP::buffer buf(2048);
      buf = 0.f;
      captureSustain(v, buf, 2048);
      return TestHelpers::measureRms(buf);
    };
    // bank[0] = sine, bank[3] = square — very different RMS/harmonic content.
    float sineRms = rmsAt(0.f);
    float squareRms = rmsAt(1.f);
    CHECK(sineRms > 0.f);
    CHECK(squareRms > 0.f);
    CHECK(std::abs(squareRms - sineRms) > 0.02f);
  }

  // ─── AKWF-style single-cycle load ───────────────────────────────────────────

  TEST_CASE("vaVoice: a 600-sample AKWF-length table loads and plays") {
    vaVoice v;
    vaParams& p = v.parameters();

    // One period of a sine at AKWF's 600-sample resolution.
    std::vector<float> cycle(600);
    for (int i = 0; i < 600; ++i)
      cycle[i] = std::sin(2.f * 3.14159265f * static_cast<float>(i) / 600.f);
    int slot = p.wavetableCount();
    p.loadWavetable(slot, cycle);
    CHECK(p.wavetableCount() == slot + 1);

    p.oscWave[0].store(YSE::SYNTH::VA_WAVETABLE);
    p.oscLevel[0].store(1.f);
    p.cutoff.store(16000.f);
    p.resonance.store(0.f);
    p.filterEnvAmount.store(0.f);
    p.keyTracking.store(0.f);
    p.ampSustain.store(1.f);
    p.wavetablePosition.store(1.f); // fully onto the newly loaded last slot

    v.frequency(69.f);
    v.velocity(1.f);
    YSE::DSP::buffer buf(2048);
    buf = 0.f;
    captureSustain(v, buf, 2048);
    CHECK(TestHelpers::measureRms(buf) > 0.02f);
  }

  // ─── velocity routing ───────────────────────────────────────────────────────

  TEST_CASE("vaVoice: velocity scales amplitude") {
    auto rmsForVel = [](float vel) {
      vaVoice v;
      vaParams& p = v.parameters();
      p.ampVelAmount.store(1.f);
      p.ampSustain.store(1.f);
      p.cutoff.store(16000.f);
      p.filterEnvAmount.store(0.f);
      v.frequency(60.f);
      v.velocity(vel);
      YSE::DSP::buffer buf(2048);
      buf = 0.f;
      captureSustain(v, buf, 2048);
      return TestHelpers::measureRms(buf);
    };
    float loud = rmsForVel(1.f);
    float soft = rmsForVel(0.3f);
    CHECK(loud > soft);
    CHECK(soft > 0.f);
  }

  // ─── clone() and shared patch ───────────────────────────────────────────────

  TEST_CASE("vaVoice: clone shares the patch but keeps independent state") {
    vaVoice proto;
    proto.parameters().cutoff.store(1234.f);

    std::unique_ptr<dspVoice> a(proto.clone());
    std::unique_ptr<dspVoice> b(proto.clone());
    auto* av = dynamic_cast<vaVoice*>(a.get());
    auto* bv = dynamic_cast<vaVoice*>(b.get());
    REQUIRE(av != nullptr);
    REQUIRE(bv != nullptr);

    // Same shared patch across the prototype and all clones.
    CHECK(av->patch().get() == proto.patch().get());
    CHECK(bv->patch().get() == proto.patch().get());
    CHECK(av->patch()->cutoff.load() == doctest::Approx(1234.f));

    // Independent note/render state: driving one leaves the other silent.
    av->frequency(60.f);
    av->velocity(1.f);
    SOUND_STATUS ia = YSE::SS_WANTSTOPLAY;
    for (int i = 0; i < 5; ++i)
      av->process(ia);
    CHECK(bv->samples[0].isSilent());
  }

  TEST_CASE("vaVoice: editing the shared patch reaches every clone") {
    vaVoice proto;
    std::unique_ptr<dspVoice> a(proto.clone());
    auto* av = dynamic_cast<vaVoice*>(a.get());
    REQUIRE(av != nullptr);
    // A control-thread edit via the retained patch pointer is visible to the clone.
    proto.patch()->resonance.store(0.42f);
    CHECK(av->patch()->resonance.load() == doctest::Approx(0.42f));
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("vaVoice: process() does not allocate after warm-up") {
    vaVoice v;
    vaParams& p = v.parameters();
    p.oscLevel[1].store(0.5f);
    p.oscWave[1].store(YSE::SYNTH::VA_PULSE);
    p.lfoToCutoff.store(1.f);
    p.lfoToPitch.store(0.5f);
    p.filterEnvAmount.store(2.f);
    v.frequency(64.f);
    v.velocity(0.8f);

    // Warm up every path: attack, sustain, release, retrigger.
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 30; ++i)
      v.process(intent);
    intent = YSE::SS_WANTSTOSTOP;
    for (int i = 0; i < 1000 && intent != YSE::SS_STOPPED; ++i)
      v.process(intent);
    intent = YSE::SS_WANTSTOPLAY;
    for (int i = 0; i < 5; ++i)
      v.process(intent);

    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 40; ++i)
        v.process(intent);
      intent = YSE::SS_WANTSTOSTOP;
      for (int i = 0; i < 1000 && intent != YSE::SS_STOPPED; ++i)
        v.process(intent);
      v.process(intent);
      intent = YSE::SS_WANTSTOPLAY;
      for (int i = 0; i < 10; ++i)
        v.process(intent);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
