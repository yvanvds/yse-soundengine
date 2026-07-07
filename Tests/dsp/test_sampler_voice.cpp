// Tests for YSE::SYNTH::samplerVoice (issue #174) — the SFZ sampler voice that
// renders the #173 region table.
//
// Coverage (acceptance + amended fixtures):
//   - pitch accuracy across the key range (±1 cent on the interpolated FFT peak),
//   - seamless looping (no click at the loop point),
//   - velocity-layer switching,
//   - ampeg envelope shape (attack rise / sustain / release decay),
//   - layered-region sum (all matching regions sound),
//   - crossfade gain law (power vs gain curve),
//   - round-robin 3-sample cycle order,
//   - hi-hat choke pair (fast + normal off_mode),
//   - multi-layer end-of-life (longest layer governs SS_STOPPED),
//   - clone() shares the immutable instrument (no PCM duplication),
//   - 16-voice polyphony renders with no audio-thread allocation,
//   - real file load through DSP::fileBuffer (resident preload).
//
// Most fixtures are built programmatically with synthetic in-memory samples, so
// the pitch/loop/envelope maths is exercised without any audio device or wav
// asset. One case loads the real test_mono_44100.wav through an .sfz.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "synth/samplerVoice.hpp"
#include "dsp/fourier/fft.hpp"
#include "dsp/math.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

#ifndef YSE_TEST_FIXTURES_DIR
#define YSE_TEST_FIXTURES_DIR "../../Tests/support/fixtures"
#endif

using YSE::SOUND_STATUS;
using YSE::SYNTH::dspVoice;
using YSE::SYNTH::residentSample;
using YSE::SYNTH::samplerConfig;
using YSE::SYNTH::samplerInstrument;
using YSE::SYNTH::samplerVoice;
namespace DSP = YSE::DSP;

namespace {

  std::string fixturesDir() {
    return std::string(YSE_TEST_FIXTURES_DIR);
  }

  // Append a synthetic single-channel sample to an instrument and return its
  // index. `gen(frame)` produces the sample value at each frame.
  template <typename Fn>
  int addSample(samplerInstrument& inst, long frames, Fn gen, float srAdjust = 1.0f) {
    residentSample rs;
    DSP::fileBuffer buf;
    buf.resize(static_cast<UInt>(frames));
    float* p = buf.getPtr();
    for (long i = 0; i < frames; ++i)
      p[i] = gen(i);
    rs.frames = frames;
    rs.sampleRateAdjustment = srAdjust;
    rs.loaded = true;
    rs.channels.push_back(buf);
    inst.samples.push_back(std::move(rs));
    return static_cast<int>(inst.samples.size()) - 1;
  }

  int addSine(samplerInstrument& inst, float hz, long frames) {
    const float sr = static_cast<float>(YSE::SAMPLERATE);
    return addSample(inst, frames, [=](long i) {
      return std::sin(2.0f * 3.14159265358979f * hz * static_cast<float>(i) / sr);
    });
  }

  int addConst(samplerInstrument& inst, float value, long frames) {
    return addSample(inst, frames, [=](long) { return value; });
  }

  // A one-region instrument playing sample `sampleIdx`.
  DSP::sfzRegion baseRegion(int sampleIdx, int keycenter = 60) {
    DSP::sfzRegion r;
    r.sampleIndex = sampleIdx;
    r.pitchKeycenter = keycenter;
    r.lokey = 0;
    r.hikey = 127;
    r.egAttack = 0.0f;
    r.egRelease = 0.005f;
    r.egSustain = 1.0f;
    r.ampVeltrack = 0.0f; // velocity does not attenuate unless a test asks for it
    return r;
  }

  // Render one WANTSTOPLAY note into `out` (N samples), warming `warm` blocks.
  unsigned captureNote(samplerVoice& v, DSP::buffer& out, unsigned N, int warm = 2) {
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < warm; ++i)
      v.process(intent);
    const unsigned block = YSE::STANDARD_BUFFERSIZE;
    unsigned filled = 0;
    while (filled + block <= N) {
      v.process(intent);
      out.copyFrom(v.samples[0], 0, filled, block);
      filled += block;
    }
    return filled;
  }

  // Interpolated fundamental frequency of a captured tone: Hann-window, FFT,
  // parabolic peak interpolation. Well under a cent for a clean tone.
  float estimateHz(DSP::buffer& sig, unsigned N) {
    DSP::buffer re(N), im(N);
    re = 0.f;
    im = 0.f;
    float* s = sig.getPtr();
    float* r = re.getPtr();
    for (unsigned i = 0; i < N; ++i) {
      float w = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979f * i / (N - 1)));
      r[i] = s[i] * w;
    }
    DSP::fft f;
    f(re, im);
    float* fr = f.getReal().getPtr();
    float* fi = f.getImaginary().getPtr();
    unsigned peak = TestHelpers::peakBinIndex(fr, fi, N);
    auto mag = [&](unsigned k) { return std::sqrt(fr[k] * fr[k] + fi[k] * fi[k]); };
    float m0 = mag(peak - 1), m1 = mag(peak), m2 = mag(peak + 1);
    float denom = (m0 - 2.0f * m1 + m2);
    float delta = denom != 0.0f ? 0.5f * (m0 - m2) / denom : 0.0f;
    float binHz = static_cast<float>(YSE::SAMPLERATE) / static_cast<float>(N);
    return (static_cast<float>(peak) + delta) * binHz;
  }

  float cents(float a, float b) {
    return 1200.0f * std::log2(a / b);
  }

  void releaseToStop(samplerVoice& v, int maxBlocks = 4000) {
    SOUND_STATUS intent = YSE::SS_WANTSTOSTOP;
    int b = 0;
    while (intent != YSE::SS_STOPPED && b < maxBlocks) {
      v.process(intent);
      ++b;
    }
  }

} // namespace

TEST_SUITE("dsp") {

  // ─── pitch accuracy across the key range ─────────────────────────────────

  TEST_CASE("samplerVoice: rendered pitch tracks the note within 1 cent") {
    const int kc = 60;
    const float f0 = DSP::MidiToFreq(static_cast<float>(kc));
    auto inst = std::make_shared<samplerInstrument>();
    int idx = addSine(*inst, f0, 4 * static_cast<long>(YSE::SAMPLERATE)); // 4 s
    DSP::sfzRegion r = baseRegion(idx, kc);
    r.loopMode = DSP::SFZ_NO_LOOP;
    inst->model.regions.push_back(r);
    inst->model.valid = true;

    for (int note : {40, 48, 60, 67, 72}) {
      samplerVoice v;
      v.setInstrument(inst);
      v.frequency(static_cast<float>(note));
      v.velocity(1.0f);
      // A long FFT + Hann-windowed parabolic peak resolves the tone to well
      // under a cent (bin width ~0.67 Hz at 44.1 kHz).
      const unsigned N = 65536;
      DSP::buffer cap(N);
      cap = 0.f;
      REQUIRE(captureNote(v, cap, N) == N);
      float hz = estimateHz(cap, N);
      float want = DSP::MidiToFreq(static_cast<float>(note));
      CHECK(std::abs(cents(hz, want)) < 1.0f);
    }
  }

  // ─── seamless looping ────────────────────────────────────────────────────

  TEST_CASE("samplerVoice: loop_continuous sustains without a click at the seam") {
    // One exact period of a sine — the whole sample is the loop region, so a
    // seamless wrap reproduces a continuous sine.
    const long period = 100; // 441 Hz at 44100
    auto inst = std::make_shared<samplerInstrument>();
    int idx = addSample(*inst, period, [=](long i) {
      return std::sin(2.0f * 3.14159265358979f * static_cast<float>(i) /
                      static_cast<float>(period));
    });
    DSP::sfzRegion r = baseRegion(idx, 60);
    r.loopMode = DSP::SFZ_LOOP_CONTINUOUS;
    r.loopStart = 0;
    r.loopEnd = period - 1;
    inst->model.regions.push_back(r);
    inst->model.valid = true;

    samplerVoice v;
    v.setInstrument(inst);
    v.frequency(60.0f); // speed 1 → reads the sample verbatim, wrapping the loop
    v.velocity(1.0f);

    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 4; ++i)
      v.process(intent); // settle the (instant) attack

    float prev = 0.f;
    bool havePrev = false;
    float maxStep = 0.f;
    float peak = 0.f;
    for (int b = 0; b < 200; ++b) { // ~580 loop wraps
      v.process(intent);
      float* out = v.samples[0].getPtr();
      for (unsigned i = 0; i < v.samples[0].getLength(); ++i) {
        if (havePrev) maxStep = std::max(maxStep, std::abs(out[i] - prev));
        peak = std::max(peak, std::abs(out[i]));
        prev = out[i];
        havePrev = true;
      }
    }
    CHECK(peak > 0.5f); // actually sounding
    // A continuous 441 Hz sine steps by at most ~2π·441/44100 ≈ 0.063 per sample.
    // A discontinuity at the loop seam would be a near-full-scale jump.
    CHECK(maxStep < 0.15f);
  }

  // ─── amplitude envelope shape ────────────────────────────────────────────

  TEST_CASE("samplerVoice: ampeg envelope shapes attack / sustain / release") {
    auto inst = std::make_shared<samplerInstrument>();
    int idx = addConst(*inst, 1.0f, static_cast<long>(YSE::SAMPLERATE)); // 1 s of DC
    DSP::sfzRegion r = baseRegion(idx, 60);
    r.egAttack = 0.05f;
    r.egDecay = 0.0f;
    r.egSustain = 0.5f;
    r.egRelease = 0.05f;
    inst->model.regions.push_back(r);
    inst->model.valid = true;

    samplerVoice v;
    v.setInstrument(inst);
    v.frequency(60.0f);
    v.velocity(1.0f);

    // With a DC=1 sample and no velocity tracking, output == the envelope.
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    float firstBlock = TestHelpers::measureRms(v.samples[0]); // mid-attack, small
    for (int i = 0; i < 60; ++i)
      v.process(intent); // reach sustain
    float sustainLvl = TestHelpers::measureRms(v.samples[0]);

    CHECK(firstBlock < sustainLvl); // attack ramps up
    CHECK(sustainLvl == doctest::Approx(0.5f).epsilon(0.05)); // holds sustain

    releaseToStop(v);
    v.process(intent = YSE::SS_STOPPED);
    CHECK(TestHelpers::decaysToSilence(v.samples[0], 1e-4f)); // release reached 0
  }

  // ─── velocity-layer switching ────────────────────────────────────────────

  TEST_CASE("samplerVoice: velocity selects the matching layer") {
    auto inst = std::make_shared<samplerInstrument>();
    int soft = addConst(*inst, 0.3f, 2000);
    int hard = addConst(*inst, 0.9f, 2000);
    DSP::sfzRegion rs = baseRegion(soft, 60);
    rs.lovel = 1;
    rs.hivel = 63;
    DSP::sfzRegion rh = baseRegion(hard, 60);
    rh.lovel = 64;
    rh.hivel = 127;
    inst->model.regions.push_back(rs); // index 0 = soft
    inst->model.regions.push_back(rh); // index 1 = hard
    inst->model.valid = true;

    samplerVoice v;
    v.setInstrument(inst);

    v.frequency(60.0f);
    v.velocity(30.0f / 127.0f);
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    CHECK(v.activeLayers() == 1);
    CHECK(v.layerRegion(0) == 0); // soft
    releaseToStop(v);

    v.frequency(60.0f);
    v.velocity(100.0f / 127.0f);
    intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    CHECK(v.activeLayers() == 1);
    CHECK(v.layerRegion(0) == 1); // hard
  }

  // ─── layered-region sum ──────────────────────────────────────────────────

  TEST_CASE("samplerVoice: overlapping regions all sound and sum") {
    auto inst = std::make_shared<samplerInstrument>();
    int a = addConst(*inst, 0.5f, 4000);
    int b = addConst(*inst, 0.5f, 4000);
    inst->model.regions.push_back(baseRegion(a, 60)); // both full-range
    inst->model.regions.push_back(baseRegion(b, 60));
    inst->model.valid = true;

    samplerVoice v;
    v.setInstrument(inst);
    v.frequency(60.0f);
    v.velocity(1.0f);
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 4; ++i)
      v.process(intent);
    CHECK(v.activeLayers() == 2);
    // 0.5 + 0.5 = 1.0 at full envelope.
    CHECK(TestHelpers::measureRms(v.samples[0]) == doctest::Approx(1.0f).epsilon(0.02));
  }

  // ─── crossfade gain law ──────────────────────────────────────────────────

  TEST_CASE("samplerVoice: velocity crossfade follows the power / gain curve") {
    auto build = [](int curve) {
      auto inst = std::make_shared<samplerInstrument>();
      int a = addConst(*inst, 1.0f, 2000);
      int b = addConst(*inst, 1.0f, 2000);
      DSP::sfzRegion r0 = baseRegion(a, 60); // fades OUT over vel 60..80
      r0.lovel = 1;
      r0.hivel = 127;
      r0.xfoutLovel = 60;
      r0.xfoutHivel = 80;
      r0.xfVelcurve = curve;
      DSP::sfzRegion r1 = baseRegion(b, 60); // fades IN over vel 60..80
      r1.lovel = 1;
      r1.hivel = 127;
      r1.xfinLovel = 60;
      r1.xfinHivel = 80;
      r1.xfVelcurve = curve;
      inst->model.regions.push_back(r0);
      inst->model.regions.push_back(r1);
      inst->model.valid = true;
      return inst;
    };

    // At velocity 70 the fade fraction is 0.5 on both layers.
    {
      samplerVoice v;
      v.setInstrument(build(DSP::SFZ_CURVE_POWER));
      v.frequency(60.0f);
      v.velocity(70.0f / 127.0f);
      SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
      v.process(intent);
      REQUIRE(v.activeLayers() == 2);
      const float eq = std::sqrt(0.5f); // equal power
      CHECK(v.layerGain(0) == doctest::Approx(eq).epsilon(0.001));
      CHECK(v.layerGain(1) == doctest::Approx(eq).epsilon(0.001));
    }
    {
      samplerVoice v;
      v.setInstrument(build(DSP::SFZ_CURVE_GAIN));
      v.frequency(60.0f);
      v.velocity(70.0f / 127.0f);
      SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
      v.process(intent);
      REQUIRE(v.activeLayers() == 2);
      CHECK(v.layerGain(0) == doctest::Approx(0.5f).epsilon(0.001)); // linear
      CHECK(v.layerGain(1) == doctest::Approx(0.5f).epsilon(0.001));
    }
  }

  // ─── round-robin cycle order ─────────────────────────────────────────────

  TEST_CASE("samplerVoice: round-robin cycles 3 samples in seq order") {
    auto inst = std::make_shared<samplerInstrument>();
    for (int k = 0; k < 3; ++k) {
      int s = addConst(*inst, 0.5f, 1000);
      DSP::sfzRegion r = baseRegion(s, 42);
      r.lokey = r.hikey = 42;
      r.seqLength = 3;
      r.seqPosition = k + 1;
      inst->model.regions.push_back(r);
    }
    inst->model.valid = true;

    samplerVoice v;
    v.setInstrument(inst);

    // Successive hits of key 42 must select regions 0,1,2,0,1,2 in order.
    for (int hit = 0; hit < 6; ++hit) {
      v.frequency(42.0f);
      v.velocity(1.0f);
      SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
      v.process(intent);
      CHECK(v.activeLayers() == 1);
      CHECK(v.layerRegion(0) == hit % 3);
      releaseToStop(v);
    }
  }

  // ─── hi-hat choke pair (fast + normal) ───────────────────────────────────

  TEST_CASE("samplerVoice: choke group silences off_by voices (fast + normal)") {
    auto buildKit = [](int openOffMode) {
      auto inst = std::make_shared<samplerInstrument>();
      int closed = addConst(*inst, 0.5f, 4000);
      int open = addConst(*inst, 0.5f, static_cast<long>(YSE::SAMPLERATE)); // long one-shot
      DSP::sfzRegion rc = baseRegion(closed, 42); // closed hat, group 1
      rc.lokey = rc.hikey = 42;
      rc.chokeGroup = 1;
      rc.offBy = 2;
      DSP::sfzRegion ro = baseRegion(open, 46); // open hat, group 2, choked by 1
      ro.lokey = ro.hikey = 46;
      ro.chokeGroup = 2;
      ro.offBy = 1;
      ro.offMode = openOffMode;
      ro.loopMode = DSP::SFZ_ONE_SHOT; // rings until choked
      inst->model.regions.push_back(rc);
      inst->model.regions.push_back(ro);
      inst->model.valid = true;
      return inst;
    };

    for (int mode : {DSP::SFZ_OFF_FAST, DSP::SFZ_OFF_NORMAL}) {
      auto inst = buildKit(mode);
      samplerVoice proto;
      proto.setInstrument(inst);
      std::unique_ptr<dspVoice> openV(proto.clone());
      std::unique_ptr<dspVoice> closedV(proto.clone());
      auto* vOpen = static_cast<samplerVoice*>(openV.get());
      auto* vClosed = static_cast<samplerVoice*>(closedV.get());

      // Open hat starts and rings.
      vOpen->frequency(46.0f);
      vOpen->velocity(1.0f);
      SOUND_STATUS iOpen = YSE::SS_WANTSTOPLAY;
      vOpen->process(iOpen);
      for (int i = 0; i < 10; ++i)
        vOpen->process(iOpen);
      REQUIRE(iOpen == YSE::SS_PLAYING);
      REQUIRE(TestHelpers::measureRms(vOpen->samples[0]) > 0.1f);

      // Closed hat fires group 1 → chokes the open hat (off_by = 1).
      vClosed->frequency(42.0f);
      vClosed->velocity(1.0f);
      SOUND_STATUS iClosed = YSE::SS_WANTSTOPLAY;
      vClosed->process(iClosed);

      // The open hat must die: fast within a few blocks, normal after its release.
      int blocks = 0;
      const int kMax = 5000;
      while (iOpen != YSE::SS_STOPPED && blocks < kMax) {
        vOpen->process(iOpen);
        ++blocks;
      }
      CHECK(iOpen == YSE::SS_STOPPED);
      if (mode == DSP::SFZ_OFF_FAST) CHECK(blocks < 8); // ~5 ms declick
    }
  }

  // ─── multi-layer end-of-life ─────────────────────────────────────────────

  TEST_CASE("samplerVoice: voice ends only when the longest layer release finishes") {
    auto inst = std::make_shared<samplerInstrument>();
    int a = addConst(*inst, 0.5f, 8000);
    int b = addConst(*inst, 0.5f, 8000);
    DSP::sfzRegion rShort = baseRegion(a, 60);
    rShort.egRelease = 0.005f; // very short tail
    DSP::sfzRegion rLong = baseRegion(b, 60);
    rLong.egRelease = 0.20f; // long tail
    inst->model.regions.push_back(rShort);
    inst->model.regions.push_back(rLong);
    inst->model.valid = true;

    samplerVoice v;
    v.setInstrument(inst);
    v.frequency(60.0f);
    v.velocity(1.0f);
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 4; ++i)
      v.process(intent);
    REQUIRE(v.activeLayers() == 2);

    // Release: the short layer finishes early but the voice keeps sounding until
    // the long layer's 200 ms release completes.
    intent = YSE::SS_WANTSTOSTOP;
    int shortGoneAt = -1, stoppedAt = -1;
    for (int b2 = 0; b2 < 4000 && intent != YSE::SS_STOPPED; ++b2) {
      v.process(intent);
      if (shortGoneAt < 0 && v.activeLayers() == 1) shortGoneAt = b2;
      if (intent == YSE::SS_STOPPED) stoppedAt = b2;
    }
    CHECK(intent == YSE::SS_STOPPED);
    CHECK(shortGoneAt >= 0);
    CHECK(stoppedAt > shortGoneAt); // longer layer outlived the shorter one
  }

  // ─── clone shares the immutable instrument ───────────────────────────────

  TEST_CASE("samplerVoice: clone shares the instrument and PCM, keeps own state") {
    auto inst = std::make_shared<samplerInstrument>();
    int idx = addConst(*inst, 0.5f, 2000);
    inst->model.regions.push_back(baseRegion(idx, 60));
    inst->model.valid = true;

    samplerVoice proto;
    proto.setInstrument(inst);
    std::unique_ptr<dspVoice> a(proto.clone());
    std::unique_ptr<dspVoice> b(proto.clone());
    auto* va = static_cast<samplerVoice*>(a.get());
    auto* vb = static_cast<samplerVoice*>(b.get());

    // Same shared instrument and same resident PCM pointer (no duplication).
    CHECK(va->instrument().get() == inst.get());
    CHECK(vb->instrument().get() == inst.get());
    CHECK(va->instrument()->samples[0].channels[0].getPtr() ==
          vb->instrument()->samples[0].channels[0].getPtr());

    // Independent render state: driving one leaves the other silent.
    va->frequency(60.0f);
    va->velocity(1.0f);
    SOUND_STATUS ia = YSE::SS_WANTSTOPLAY;
    for (int i = 0; i < 4; ++i)
      va->process(ia);
    CHECK(vb->samples[0].isSilent());
  }

  // ─── no region matched → clean drop ──────────────────────────────────────

  TEST_CASE("samplerVoice: a note with no matching region drops to STOPPED") {
    auto inst = std::make_shared<samplerInstrument>();
    int idx = addConst(*inst, 0.5f, 1000);
    DSP::sfzRegion r = baseRegion(idx, 60);
    r.lokey = r.hikey = 60; // only key 60
    inst->model.regions.push_back(r);
    inst->model.valid = true;

    samplerVoice v;
    v.setInstrument(inst);
    v.frequency(48.0f); // no region covers key 48
    v.velocity(1.0f);
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    CHECK(intent == YSE::SS_STOPPED);
    CHECK(v.samples[0].isSilent());
  }

  // ─── 16-voice polyphony: no audio-thread allocation ──────────────────────

  TEST_CASE("samplerVoice: 16-voice polyphony renders without audio-thread alloc") {
    auto inst = std::make_shared<samplerInstrument>();
    int lo = addConst(*inst, 0.5f, 8000);
    int hi = addSine(*inst, 440.0f, 8000);
    DSP::sfzRegion r0 = baseRegion(lo, 60);
    r0.lovel = 1;
    r0.hivel = 100;
    r0.loopMode = DSP::SFZ_LOOP_CONTINUOUS;
    r0.loopStart = 0;
    r0.loopEnd = 7999;
    DSP::sfzRegion r1 = baseRegion(hi, 60); // overlaps → layered voice
    r1.lovel = 1;
    r1.hivel = 127;
    inst->model.regions.push_back(r0);
    inst->model.regions.push_back(r1);
    inst->model.valid = true;

    samplerVoice proto;
    proto.setInstrument(inst);
    std::vector<std::unique_ptr<dspVoice>> voices;
    for (int i = 0; i < 16; ++i)
      voices.emplace_back(proto.clone());

    auto drive = [&](bool underProbe) {
      for (int i = 0; i < 16; ++i) {
        auto* v = static_cast<samplerVoice*>(voices[i].get());
        v->frequency(static_cast<float>(48 + i));
        v->velocity(0.8f);
        SOUND_STATUS in = YSE::SS_WANTSTOPLAY;
        v->process(in);
        for (int b = 0; b < 6; ++b)
          v->process(in);
        in = YSE::SS_WANTSTOSTOP;
        for (int b = 0; b < 200 && in != YSE::SS_STOPPED; ++b)
          v->process(in);
        (void)underProbe;
      }
    };

    drive(false); // warm every path (attack / loop / release / retrigger)

    {
      TestHelpers::ProbeScope probe;
      drive(true);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // ─── real file load through the resident preload path ────────────────────

  TEST_CASE("samplerVoice: loads a real .sfz and renders its PCM (resident preload)") {
    // sfz_dedup.sfz names the real test_mono_44100.wav twice (de-duped by #173).
    samplerVoice v;
    bool ok = v.loadSFZ(fixturesDir() + "/sfz_dedup.sfz");
    REQUIRE(ok);
    REQUIRE(v.instrument()->valid());
    // One de-duplicated, resident sample with actual PCM frames.
    REQUIRE(v.instrument()->samples.size() == 1);
    CHECK(v.instrument()->samples[0].loaded);
    CHECK(v.instrument()->samples[0].frames > 0);

    v.frequency(60.0f); // key 60 is covered by the c4 region
    v.velocity(1.0f);
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    CHECK(intent == YSE::SS_PLAYING);
    for (int i = 0; i < 4; ++i)
      v.process(intent);
    // The voice resolved a layer and is rendering (PCM decoded, not silent stub).
    CHECK(v.activeLayers() >= 1);
  }

  // ─── samplerConfig facade ────────────────────────────────────────────────

  TEST_CASE("samplerVoice: samplerConfig builds a one-region instrument from a file") {
    std::string wav = fixturesDir() + "/test_mono_44100.wav";
    samplerConfig cfg;
    cfg.name("test").file(wav.c_str()).root(60).range(48, 72).envelope(0.0f, 0.1f, 5.0f);

    samplerVoice v;
    REQUIRE(v.configure(cfg));
    REQUIRE(v.instrument()->valid());
    REQUIRE(v.instrument()->model.regions.size() == 1);
    CHECK(v.instrument()->model.regions[0].pitchKeycenter == 60);
    CHECK(v.instrument()->model.regions[0].lokey == 48);
    CHECK(v.instrument()->model.regions[0].hikey == 72);

    v.frequency(60.0f);
    v.velocity(1.0f);
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 4; ++i)
      v.process(intent);
    CHECK(v.activeLayers() == 1);
  }

} // TEST_SUITE("dsp")
