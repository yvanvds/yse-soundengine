// Behavioural tests for the Dattorro plate reverb MODULE (issue #162).
//
// plateReverb is an insert/return-grade reverb built on the Dattorro plate
// topology: a four-allpass input diffuser feeding a cross-coupled figure-eight
// tank (two symmetric halves, each a modulated allpass + delay + damping
// low-pass + allpass + delay). Mono is downmixed into the tank; a stereo pair
// is read from seven fixed node taps. The wet/dry balance is the inherited
// impact(); with impact = 1 the output is the pure wet reverb, so an impulse in
// produces a decaying, diffuse tail.
//
// The acceptance criteria (issue #162) drive the cases below:
//   - RT60 scales with the decay parameter (measured on a rendered impulse)
//   - damping affects HF decay; pre-delay offsets the first reflection
//   - no allocation in process(); output stays bounded
//
// No audio device required — SAMPLERATE is initialised by the
// portaudioDeviceManager translation unit at static-initialisation time (44100
// by default; CI also forces 48000 via YSE_TEST_FORCED_RATE). Every tone,
// window, and sample count below is derived from the *actual* SAMPLERATE so the
// tests hold at any rate.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules/plateReverb.hpp"
#include "headers/defines.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

static constexpr float kPi = 3.14159265358979323846f;

namespace {

  // The engine sample rate this run is using (honours the forced-rate env).
  inline float sr() {
    return static_cast<float>(YSE::SAMPLERATE);
  }

  // Milliseconds -> whole audio blocks of the given block size (rounded up), so
  // observation windows are wall-clock durations independent of the sample rate.
  inline int blocksForMs(float ms, unsigned blockSize) {
    float samples = ms * 0.001f * sr();
    return static_cast<int>(std::ceil(samples / static_cast<float>(blockSize)));
  }

  inline void zeroFill(YSE::DSP::buffer& buf) {
    buf = 0.0f;
  }

  inline void fillSine(YSE::DSP::buffer& buf, float freq) {
    float* p = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
      p[i] = std::sin(2.0f * kPi * freq * static_cast<float>(i) / sr());
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

  // Drive one broadband impulse through the reverb, then run silence and sum the
  // wet RMS of every block over `windowMs` of wall-clock time. A longer tail
  // integrates more energy, so this is a monotone proxy for RT60.
  float tailEnergy(YSE::DSP::MODULES::plateReverb& rev, float windowMs, unsigned blockSize = 128) {
    MULTICHANNELBUFFER buf(2);
    buf[0].resize(blockSize);
    buf[1].resize(blockSize);
    zeroFill(buf[0]);
    zeroFill(buf[1]);
    buf[0].getPtr()[0] = 1.0f;
    buf[1].getPtr()[0] = 1.0f;
    rev.process(buf);

    float energy = 0.0f;
    int blocks = blocksForMs(windowMs, blockSize);
    for (int iter = 0; iter < blocks; ++iter) {
      zeroFill(buf[0]);
      zeroFill(buf[1]);
      rev.process(buf);
      energy += TestHelpers::measureRms(buf[0]) + TestHelpers::measureRms(buf[1]);
    }
    return energy;
  }

} // anonymous namespace

TEST_SUITE("dsp") {

  // ─── parameters ───────────────────────────────────────────────────────────────

  TEST_CASE("plateReverb: sensible defaults") {
    YSE::DSP::MODULES::plateReverb rev;
    CHECK(rev.decay() == doctest::Approx(0.5f).epsilon(1e-5f));
    CHECK(rev.damping() == doctest::Approx(8000.0f).epsilon(1e-5f));
    CHECK(rev.preDelay() == doctest::Approx(0.0f).epsilon(1e-5f));
  }

  TEST_CASE("plateReverb: setter/getter round-trips") {
    YSE::DSP::MODULES::plateReverb rev;
    rev.decay(0.8f).damping(3000.0f).preDelay(20.0f);
    CHECK(rev.decay() == doctest::Approx(0.8f).epsilon(1e-5f));
    CHECK(rev.damping() == doctest::Approx(3000.0f).epsilon(1e-5f));
    CHECK(rev.preDelay() == doctest::Approx(20.0f).epsilon(1e-5f));
  }

  TEST_CASE("plateReverb: parameters are clamped to safe ranges") {
    YSE::DSP::MODULES::plateReverb rev;
    rev.decay(5.0f); // above the stable maximum
    CHECK(rev.decay() <= 0.98f);
    rev.decay(-1.0f);
    CHECK(rev.decay() == doctest::Approx(0.0f));
    rev.preDelay(1.0e9f); // absurd — clamp to the line maximum
    CHECK(rev.preDelay() <= 100.0f);
    rev.preDelay(-5.0f);
    CHECK(rev.preDelay() == doctest::Approx(0.0f));
    rev.damping(1.0e9f); // clamp below Nyquist
    CHECK(rev.damping() <= 0.5f * sr());
  }

  // ─── impulse response ─────────────────────────────────────────────────────────

  TEST_CASE("plateReverb: an impulse produces a diffuse wet tail") {
    YSE::DSP::MODULES::plateReverb rev;
    rev.decay(0.7f).damping(10000.0f).impact(1.0f);

    MULTICHANNELBUFFER buf(2);
    buf[0].resize(128);
    buf[1].resize(128);
    zeroFill(buf[0]);
    zeroFill(buf[1]);
    buf[0].getPtr()[0] = 1.0f;
    buf[1].getPtr()[0] = 1.0f;
    rev.process(buf);

    // Within ~120 ms of wall clock the tank must fill and surface wet energy on
    // both output channels (the figure-eight cross-couples into both halves).
    bool sawLeft = false, sawRight = false;
    int blocks = blocksForMs(120.0f, 128);
    for (int iter = 0; iter < blocks; ++iter) {
      zeroFill(buf[0]);
      zeroFill(buf[1]);
      rev.process(buf);
      if (TestHelpers::measureRms(buf[0]) > 1e-4f) sawLeft = true;
      if (TestHelpers::measureRms(buf[1]) > 1e-4f) sawRight = true;
    }
    CHECK(sawLeft);
    CHECK(sawRight);
  }

  // ─── RT60 scales with decay (acceptance) ────────────────────────────────────────

  TEST_CASE("plateReverb: higher decay lengthens the tail (RT60)") {
    // Same impulse, same damping, only the decay parameter differs. A larger
    // decay recirculates the tank longer, so its integrated tail energy over a
    // fixed wall-clock window is strictly greater.
    auto energyFor = [](float decay) {
      YSE::DSP::MODULES::plateReverb rev;
      rev.decay(decay).damping(20000.0f).impact(1.0f);
      return tailEnergy(rev, 600.0f);
    };
    CHECK(energyFor(0.85f) > energyFor(0.4f));
  }

  // ─── damping affects HF decay (acceptance) ──────────────────────────────────────

  TEST_CASE("plateReverb: damping darkens the tail (HF decay)") {
    // Excite the tank with a bright tone, then let it ring. The heavily-damped
    // plate sheds high-frequency energy in the loop faster, so its steady-state
    // tail RMS is lower than the barely-damped one at the same decay.
    auto tailAfterBright = [](float dampHz) {
      YSE::DSP::MODULES::plateReverb rev;
      rev.decay(0.8f).damping(dampHz).impact(1.0f);
      MULTICHANNELBUFFER buf(2);
      buf[0].resize(128);
      buf[1].resize(128);
      // A high tone (a quarter of Nyquist) is bright at any sample rate.
      float bright = 0.25f * sr();
      for (int iter = 0; iter < 8; ++iter) {
        fillSine(buf[0], bright);
        fillSine(buf[1], bright);
        rev.process(buf);
      }
      float energy = 0.0f;
      int blocks = blocksForMs(300.0f, 128);
      for (int iter = 0; iter < blocks; ++iter) {
        zeroFill(buf[0]);
        zeroFill(buf[1]);
        rev.process(buf);
        energy += TestHelpers::measureRms(buf[0]) + TestHelpers::measureRms(buf[1]);
      }
      return energy;
    };
    CHECK(tailAfterBright(18000.0f) > tailAfterBright(1500.0f));
  }

  // ─── pre-delay offsets the onset (acceptance) ───────────────────────────────────

  TEST_CASE("plateReverb: pre-delay offsets the reverb onset") {
    // With a substantial pre-delay the tank is fed pure silence for the
    // pre-delay duration, so the wet output stays *exactly* zero until the
    // delayed impulse reaches the tank. Detecting the first non-silent block
    // (output is bit-zero before arrival) isolates the pre-delay offset cleanly:
    // the pre-delayed plate's onset must land strictly later.
    auto onsetBlock = [](float preMs) {
      YSE::DSP::MODULES::plateReverb rev;
      rev.decay(0.7f).damping(15000.0f).preDelay(preMs).impact(1.0f);
      MULTICHANNELBUFFER buf(2);
      buf[0].resize(64);
      buf[1].resize(64);
      zeroFill(buf[0]);
      zeroFill(buf[1]);
      buf[0].getPtr()[0] = 1.0f;
      buf[1].getPtr()[0] = 1.0f;
      rev.process(buf);
      int block = 1;
      int limit = blocksForMs(400.0f, 64);
      for (; block < limit; ++block) {
        zeroFill(buf[0]);
        zeroFill(buf[1]);
        rev.process(buf);
        if (TestHelpers::measureRms(buf[0]) > 1e-6f) break;
      }
      return block;
    };
    // A ~40 ms extra pre-delay is many blocks of silence at 64 samples/block,
    // comfortably clear of the first-arrival jitter.
    CHECK(onsetBlock(80.0f) > onsetBlock(0.0f) + 2);
  }

  // ─── stability ──────────────────────────────────────────────────────────────────

  TEST_CASE("plateReverb: output stays bounded for sustained input") {
    YSE::DSP::MODULES::plateReverb rev;
    rev.decay(0.98f).damping(6000.0f).impact(1.0f); // hottest stable setting

    MULTICHANNELBUFFER buf(2);
    buf[0].resize(128);
    buf[1].resize(128);
    for (int iter = 0; iter < 400; ++iter) {
      fillSine(buf[0], 440.0f);
      fillSine(buf[1], 660.0f);
      rev.process(buf);
      CHECK(maxAbs(buf[0]) < 20.0f);
      CHECK(maxAbs(buf[1]) < 20.0f);
    }
  }

  TEST_CASE("plateReverb: tail decays to silence after input stops") {
    YSE::DSP::MODULES::plateReverb rev;
    rev.decay(0.7f).damping(8000.0f).impact(1.0f);
    MULTICHANNELBUFFER buf(2);
    buf[0].resize(128);
    buf[1].resize(128);
    // Drive a short burst.
    for (int iter = 0; iter < 8; ++iter) {
      fillSine(buf[0], 500.0f);
      fillSine(buf[1], 500.0f);
      rev.process(buf);
    }
    // Then a long silence; the tail must decay toward zero (decay < 1).
    int blocks = blocksForMs(6000.0f, 128);
    for (int iter = 0; iter < blocks; ++iter) {
      zeroFill(buf[0]);
      zeroFill(buf[1]);
      rev.process(buf);
    }
    CHECK(TestHelpers::measureRms(buf[0]) < 1e-3f);
    CHECK(TestHelpers::measureRms(buf[1]) < 1e-3f);
  }

  // ─── wet/dry mix ────────────────────────────────────────────────────────────────

  TEST_CASE("plateReverb: impact(0) is a passthrough") {
    YSE::DSP::MODULES::plateReverb rev;
    rev.decay(0.7f).impact(0.0f); // fully dry
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    fillSine(buf[0], 300.0f);
    YSE::DSP::buffer expected(128);
    expected = buf[0];
    rev.process(buf);
    CHECK(TestHelpers::buffersNearlyEqual(buf[0], expected, 1e-5f));
  }

  // ─── N-channel behaviour (defined here per the issue) ────────────────────────────

  TEST_CASE("plateReverb: mono downmix produces a wet tail") {
    YSE::DSP::MODULES::plateReverb rev;
    rev.decay(0.7f).damping(12000.0f).impact(1.0f);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    zeroFill(buf[0]);
    buf[0].getPtr()[0] = 1.0f;
    rev.process(buf);
    bool sawTail = false;
    int blocks = blocksForMs(200.0f, 128);
    for (int iter = 0; iter < blocks; ++iter) {
      zeroFill(buf[0]);
      rev.process(buf);
      if (TestHelpers::measureRms(buf[0]) > 1e-4f) {
        sawTail = true;
        break;
      }
    }
    CHECK(sawTail);
  }

  TEST_CASE("plateReverb: even/odd channels carry the stereo tank outputs") {
    // Four channels: even (0, 2) get the left tank output, odd (1, 3) the right.
    // The two halves are decorrelated, so channels of the same parity match and
    // opposite parity differ — and all four carry non-trivial wet energy.
    YSE::DSP::MODULES::plateReverb rev;
    rev.decay(0.75f).damping(12000.0f).impact(1.0f);
    MULTICHANNELBUFFER buf(4);
    for (int c = 0; c < 4; ++c)
      buf[c].resize(128);
    // Impulse on every channel so the mono downmix is well excited.
    for (int c = 0; c < 4; ++c) {
      zeroFill(buf[c]);
      buf[c].getPtr()[0] = 1.0f;
    }
    rev.process(buf);
    bool sawEnergy = false;
    int blocks = blocksForMs(200.0f, 128);
    for (int iter = 0; iter < blocks && !sawEnergy; ++iter) {
      for (int c = 0; c < 4; ++c)
        zeroFill(buf[c]);
      rev.process(buf);
      if (TestHelpers::measureRms(buf[0]) > 1e-3f) {
        sawEnergy = true;
        // Same-parity channels are bit-identical; opposite parity decorrelated.
        CHECK(TestHelpers::buffersNearlyEqual(buf[0], buf[2], 1e-6f));
        CHECK(TestHelpers::buffersNearlyEqual(buf[1], buf[3], 1e-6f));
        CHECK_FALSE(TestHelpers::buffersNearlyEqual(buf[0], buf[1], 1e-4f));
      }
    }
    CHECK(sawEnergy);
  }

  // ─── real-time discipline (acceptance) ───────────────────────────────────────────

  TEST_CASE("plateReverb: steady-state processing is allocation-free") {
    YSE::DSP::MODULES::plateReverb rev;
    rev.decay(0.8f).damping(6000.0f).preDelay(20.0f).impact(1.0f);
    MULTICHANNELBUFFER buf(2);
    buf[0].resize(128);
    buf[1].resize(128);

    // Warm up: the one-time create()/sizing allocation happens here.
    for (int b = 0; b < 6; ++b) {
      fillSine(buf[0], 440.0f);
      fillSine(buf[1], 660.0f);
      rev.process(buf);
    }

    // Steady state: no allocation allowed on the audio path.
    {
      TestHelpers::ProbeScope probe;
      for (int b = 0; b < 8; ++b) {
        fillSine(buf[0], 440.0f);
        fillSine(buf[1], 660.0f);
        rev.process(buf);
      }
    }
    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

  // ─── sample-rate robustness ──────────────────────────────────────────────────────

  TEST_CASE("plateReverb: tolerates a change in input buffer length") {
    YSE::DSP::MODULES::plateReverb rev;
    rev.decay(0.6f).impact(1.0f);

    MULTICHANNELBUFFER buf(2);
    buf[0].resize(64);
    buf[1].resize(64);
    fillSine(buf[0], 500.0f);
    fillSine(buf[1], 500.0f);
    rev.process(buf);
    CHECK(buf[0].getLength() == 64u);

    buf[0].resize(256);
    buf[1].resize(256);
    fillSine(buf[0], 500.0f);
    fillSine(buf[1], 500.0f);
    rev.process(buf);
    CHECK(buf[0].getLength() == 256u);
    // Still bounded after the block-size change.
    CHECK(maxAbs(buf[0]) < 20.0f);
  }

} // TEST_SUITE("dsp")
