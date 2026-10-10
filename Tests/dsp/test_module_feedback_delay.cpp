// Behavioural tests for the multichannel feedback delay MODULE (issue #160).
//
// feedbackDelay is a recirculating delay: a per-channel delay line with a
// feedback path, a damping low-pass in that path, and cross-feed between
// channel pairs (ping-pong). The wet/dry balance is the inherited impact();
// with the default impact = 1 the output is the pure delayed (wet) signal, so
// an impulse in produces a train of decaying echoes on the output.
//
// The underlying DSP::delay reads in *milliseconds* quantised to samples
// (delaySamples = SAMPLERATE * ms * 0.001), so at 44100 Hz a 5 ms tap is
// ~220 samples ~= 1.7 blocks of 128.
//
// No audio device required — SAMPLERATE is initialised to 48000 by the
// portaudioDeviceManager translation unit at static-initialisation time.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules/delay/feedbackDelay.hpp"
#include "headers/defines.hpp"
#include "support/audio_helpers.hpp"

static constexpr float kPi = 3.14159265358979323846f;

namespace {

  inline void fillSine(YSE::DSP::buffer& buf, float freq, float sr = 44100.0f) {
    float* p = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
      p[i] = std::sin(2.0f * kPi * freq * static_cast<float>(i) / sr);
  }

  inline void zeroFill(YSE::DSP::buffer& buf) {
    buf = 0.0f;
  }

  inline float maxAbs(YSE::DSP::buffer& buf) {
    float* p = buf.getPtr();
    float m = 0.0f;
    for (unsigned i = 0; i < buf.getLength(); ++i) {
      float a = std::abs(p[i]);
      if (a > m) m = a;
    }
    return m;
  }

  // Phase-continuous sine generator so the *input* stream has no block-boundary
  // discontinuity of its own — any jump in the output then comes purely from the
  // delay, which is exactly what the click-free test wants to isolate.
  struct SineGen {
    double phase = 0.0;
    float freq;
    float sr;
    explicit SineGen(float f, float s = 44100.0f) : freq(f), sr(s) {}
    void fill(YSE::DSP::buffer& buf) {
      float* p = buf.getPtr();
      double inc = 2.0 * static_cast<double>(kPi) * freq / sr;
      for (unsigned i = 0; i < buf.getLength(); ++i) {
        p[i] = static_cast<float>(std::sin(phase));
        phase += inc;
      }
    }
  };

} // anonymous namespace

TEST_SUITE("dsp") {

  // ─── parameters ───────────────────────────────────────────────────────────────

  TEST_CASE("feedbackDelay: sensible defaults") {
    YSE::DSP::MODULES::feedbackDelay d;
    CHECK(d.time() == doctest::Approx(250.0f).epsilon(1e-5f));
    CHECK(d.feedback() == doctest::Approx(0.5f).epsilon(1e-5f));
    CHECK(d.damping() == doctest::Approx(8000.0f).epsilon(1e-5f));
    CHECK(d.crossfeed() == doctest::Approx(0.0f).epsilon(1e-5f));
  }

  TEST_CASE("feedbackDelay: setter/getter round-trips") {
    YSE::DSP::MODULES::feedbackDelay d;
    d.time(120.0f).feedback(0.7f).damping(3000.0f).crossfeed(0.25f);
    CHECK(d.time() == doctest::Approx(120.0f).epsilon(1e-5f));
    CHECK(d.feedback() == doctest::Approx(0.7f).epsilon(1e-5f));
    CHECK(d.damping() == doctest::Approx(3000.0f).epsilon(1e-5f));
    CHECK(d.crossfeed() == doctest::Approx(0.25f).epsilon(1e-5f));
  }

  TEST_CASE("feedbackDelay: parameters are clamped to safe ranges") {
    YSE::DSP::MODULES::feedbackDelay d;
    d.feedback(5.0f); // above the stable maximum
    CHECK(d.feedback() <= 0.99f);
    d.feedback(-1.0f);
    CHECK(d.feedback() == doctest::Approx(0.0f));
    d.crossfeed(3.0f);
    CHECK(d.crossfeed() == doctest::Approx(1.0f));
    d.crossfeed(-1.0f);
    CHECK(d.crossfeed() == doctest::Approx(0.0f));
    d.time(1.0e9f); // absurd — clamp to the line maximum
    CHECK(d.time() <= 2000.0f);
    d.time(0.0f);
    CHECK(d.time() >= 1.0f);
  }

  // ─── impulse response ───────────────────────────────────────────────────────────

  TEST_CASE("feedbackDelay: an impulse produces a delayed echo (tap time)") {
    YSE::DSP::MODULES::feedbackDelay d;
    d.time(5.0f).feedback(0.5f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);

    // Feed one impulse block, then silence.
    zeroFill(buf[0]);
    buf[0].getPtr()[0] = 1.0f;
    d.process(buf);
    // The wet tap for the first block reads silence (line just primed).
    CHECK(TestHelpers::measureRms(buf[0]) < 1e-4f);

    // Within the first ~10 blocks (~29 ms) the delayed echo must surface.
    bool sawEcho = false;
    for (int iter = 0; iter < 10; ++iter) {
      zeroFill(buf[0]);
      d.process(buf);
      if (TestHelpers::measureRms(buf[0]) > 1e-3f) {
        sawEcho = true;
        break;
      }
    }
    CHECK(sawEcho);
  }

  TEST_CASE("feedbackDelay: higher feedback sustains a longer echo tail") {
    auto tailEnergy = [](float fb) {
      YSE::DSP::MODULES::feedbackDelay d;
      d.time(5.0f).feedback(fb).damping(20000.0f);
      MULTICHANNELBUFFER buf(1);
      buf[0].resize(128);
      zeroFill(buf[0]);
      buf[0].getPtr()[0] = 1.0f;
      d.process(buf);
      float energy = 0.0f;
      for (int iter = 0; iter < 60; ++iter) {
        zeroFill(buf[0]);
        d.process(buf);
        energy += TestHelpers::measureRms(buf[0]);
      }
      return energy;
    };
    // A near-unity feedback recirculates the impulse many more times than a
    // low feedback, so its accumulated tail energy is strictly larger.
    CHECK(tailEnergy(0.85f) > tailEnergy(0.2f));
  }

  TEST_CASE("feedbackDelay: damping darkens the recirculating tail") {
    // Two identical feedback delays fed the same broadband impulse; the one
    // with a low damping cut-off loses high-frequency energy in the loop, so
    // its steady-state tail RMS is lower than the barely-damped one.
    auto tailEnergy = [](float dampHz) {
      YSE::DSP::MODULES::feedbackDelay d;
      d.time(4.0f).feedback(0.8f).damping(dampHz);
      MULTICHANNELBUFFER buf(1);
      buf[0].resize(128);
      // Excite with a bright signal (high-frequency sine) for several blocks.
      for (int iter = 0; iter < 4; ++iter) {
        fillSine(buf[0], 9000.0f);
        d.process(buf);
      }
      float energy = 0.0f;
      for (int iter = 0; iter < 40; ++iter) {
        zeroFill(buf[0]);
        d.process(buf);
        energy += TestHelpers::measureRms(buf[0]);
      }
      return energy;
    };
    CHECK(tailEnergy(20000.0f) > tailEnergy(500.0f));
  }

  // ─── cross-feed routing ─────────────────────────────────────────────────────────

  TEST_CASE("feedbackDelay: cross-feed routes echoes into the partner channel") {
    // Two channels; feed an impulse only into channel 0. With full cross-feed
    // the recirculated echo bounces into channel 1, so channel 1 must show
    // non-trivial energy over time. With no cross-feed channel 1 stays silent.
    auto partnerEnergy = [](float cross) {
      YSE::DSP::MODULES::feedbackDelay d;
      d.time(4.0f).feedback(0.7f).crossfeed(cross).damping(20000.0f);
      MULTICHANNELBUFFER buf(2);
      buf[0].resize(128);
      buf[1].resize(128);
      zeroFill(buf[0]);
      zeroFill(buf[1]);
      buf[0].getPtr()[0] = 1.0f; // impulse on channel 0 only
      d.process(buf);
      float ch1Energy = 0.0f;
      for (int iter = 0; iter < 40; ++iter) {
        zeroFill(buf[0]);
        zeroFill(buf[1]);
        d.process(buf);
        ch1Energy += TestHelpers::measureRms(buf[1]);
      }
      return ch1Energy;
    };
    CHECK(partnerEnergy(0.0f) < 1e-5f); // independent: partner stays silent
    CHECK(partnerEnergy(1.0f) > 1e-3f); // ping-pong: energy crosses over
  }

  // ─── click-free modulation & stability ──────────────────────────────────────────

  TEST_CASE("feedbackDelay: abrupt delay-time changes are click-free") {
    // Feed a phase-continuous sine (feedback off so the wet amplitude stays
    // <= 1) and make large, abrupt jumps in the delay time. The per-sample
    // smoother must slew the read position gradually, so the output stays
    // continuous *across block boundaries too* — an unsmoothed step would jump
    // the read pointer thousands of samples in one hop and produce a
    // discontinuity approaching the signal's peak-to-peak (~2.0).
    YSE::DSP::MODULES::feedbackDelay d;
    d.feedback(0.0f).damping(20000.0f).impact(1.0f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    SineGen gen(300.0f);

    // Warm up at a fixed time so the delay line has continuous content.
    d.time(30.0f);
    for (int iter = 0; iter < 40; ++iter) {
      gen.fill(buf[0]);
      d.process(buf);
    }

    float prev = buf[0].getPtr()[buf[0].getLength() - 1];
    float worstStep = 0.0f;
    const float targets[] = {180.0f, 25.0f, 200.0f, 20.0f};
    for (float target : targets) {
      d.time(target); // one abrupt jump
      for (int iter = 0; iter < 50; ++iter) {
        gen.fill(buf[0]);
        d.process(buf);
        float* p = buf[0].getPtr();
        // Continuity across the block boundary (where an unsmoothed step clicks).
        float boundary = std::abs(p[0] - prev);
        if (boundary > worstStep) worstStep = boundary;
        // Continuity within the block.
        for (unsigned i = 1; i < buf[0].getLength(); ++i) {
          float step = std::abs(p[i] - p[i - 1]);
          if (step > worstStep) worstStep = step;
        }
        prev = p[buf[0].getLength() - 1];
      }
    }
    // Smoothed pitch-shift transients stay well under half scale; a hard delay
    // jump would spike toward ~2.0.
    CHECK(worstStep < 0.5f);
  }

  TEST_CASE("feedbackDelay: output stays bounded for sustained input") {
    YSE::DSP::MODULES::feedbackDelay d;
    d.time(7.0f).feedback(0.9f).crossfeed(0.5f).damping(6000.0f);

    MULTICHANNELBUFFER buf(2);
    buf[0].resize(128);
    buf[1].resize(128);
    for (int iter = 0; iter < 200; ++iter) {
      fillSine(buf[0], 440.0f);
      fillSine(buf[1], 660.0f);
      d.process(buf);
      CHECK(maxAbs(buf[0]) < 20.0f);
      CHECK(maxAbs(buf[1]) < 20.0f);
    }
  }

  // ─── time smoother tau survives a sample-rate change (issue #637) ─────────────

  // Exact read-out of the delay-time smoother's time constant, in ms.
  //
  // Protocol: feed a global ramp x[n] = n. The underlying DSP::delay read is a
  // direct (integer-quantised) sample fetch, so with feedback 0 / impact 1 the
  // output at sample i is the ramp value at (n_i - trunc(sr * t_i / 1000)) —
  // decoding the per-sample smoothed delay time t_i. A time() step then exposes
  // the one-pole coefficient through the decay ratio across one block.
  //
  // The module is constructed (and create() run) at `constructRate`, then run
  // at `runRate` — the unit-test stand-in for a close()/init() cycle.
  static double feedbackDelayTauMs(UInt constructRate, UInt runRate) {
    TestHelpers::ScopedSampleRate atConstruct(constructRate);
    YSE::DSP::MODULES::feedbackDelay d;
    d.time(400.0f).feedback(0.0f).crossfeed(0.0f).impact(1.0f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    double n = 0.0; // global ramp position
    auto feedRamp = [&]() {
      float* p = buf[0].getPtr();
      for (unsigned i = 0; i < 128; ++i)
        p[i] = static_cast<float>(n + i);
    };

    feedRamp();
    d.process(buf); // triggers create() -> smoother coef derived at constructRate
    n += 128.0;

    TestHelpers::ScopedSampleRate atRun(runRate);
    // Refill the line past the 400 ms working delay at the run rate so the
    // read decodes ramp history rather than stale/cleared samples.
    const int primeBlocks = static_cast<int>(0.4 * runRate / 128.0) + 4;
    for (int b = 0; b < primeBlocks; ++b) {
      feedRamp();
      d.process(buf);
      n += 128.0;
    }

    // Step the target time and capture one block mid-glide.
    d.time(100.0f);
    feedRamp();
    d.process(buf);
    n += 128.0;

    const double srMs = 0.001 * static_cast<double>(runRate);
    const double targetMs = 100.0;
    const float* out = buf[0].getPtr();
    // n has already advanced past this block, so sample i sits at n - 128 + i.
    const double base = n - 128.0;
    const double d0 = (base + 0.0 - static_cast<double>(out[0])) / srMs - targetMs;
    const double d127 = (base + 127.0 - static_cast<double>(out[127])) / srMs - targetMs;
    const double lnOneMinusCoef = std::log(d127 / d0) / 127.0;
    return -1000.0 / (static_cast<double>(runRate) * lnOneMinusCoef);
  }

  TEST_CASE("feedbackDelay: time smoother keeps its 30 ms tau across a rate change (issue #637)") {
    // Baseline: built and run at one rate, the documented TIME_SMOOTH_TAU.
    CHECK(feedbackDelayTauMs(48000, 48000) == doctest::Approx(30.0).epsilon(0.03).scale(0.0));
    // create() runs once per instance, so before the fix the coefficient kept
    // the construction rate across a close()/init() cycle: ~65 ms of wall
    // clock when built at 44.1 kHz and run at 96 kHz (and ~14 ms the other
    // way). It must follow the rate the module is *run* at.
    CHECK(feedbackDelayTauMs(44100, 96000) == doctest::Approx(30.0).epsilon(0.03).scale(0.0));
    CHECK(feedbackDelayTauMs(96000, 44100) == doctest::Approx(30.0).epsilon(0.03).scale(0.0));
  }

  TEST_CASE("feedbackDelay: tolerates a change in input buffer length") {
    YSE::DSP::MODULES::feedbackDelay d;
    d.time(6.0f).feedback(0.5f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(64);
    fillSine(buf[0], 500.0f);
    d.process(buf);
    CHECK(buf[0].getLength() == 64u);

    buf[0].resize(256);
    fillSine(buf[0], 500.0f);
    d.process(buf);
    CHECK(buf[0].getLength() == 256u);
  }

} // TEST_SUITE("dsp")
