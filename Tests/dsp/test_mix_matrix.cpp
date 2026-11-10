// Tests for YSE::DSP::mixMatrix (YseEngine/dsp/routing/mixMatrix.hpp), the
// signal matrix behind ~matrix (issue #1134): sizes and clamping, steady
// mixing exact against the sum of the inputs, the linear gain ramp in closed
// form at the live sample rate (48 and 96 kHz, a rate change after
// construction and during a ramp), a change during a ramp, per-change ramp
// times, connect / disconnect / clear, the exclusive modes, ignored input,
// block lengths, and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include "dsp/routing/mixMatrix.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::mixMatrix;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // A signal that differs per input and per sample, so a wrong input or a
  // wrong sample shows.
  void FillInput(buffer& b, unsigned int input) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = static_cast<float>((input + 1) * 1000 + i);
  }

  // The gain of a ramp from `from` to `to` over `samples` samples, at sample
  // `k` counted from the first sample processed after the change.
  float RampAt(float from, float to, double samples, unsigned int k) {
    const double phase = samples > 1.0 ? std::min(1.0, (k + 1) / samples) : 1.0;
    return from + ((to - from) * static_cast<float>(phase));
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("mixMatrix: sizes, defaults and clamping (#1134)") {
    mixMatrix a;
    CHECK(a.inputs() == 1);
    CHECK(a.outputs() == 1);
    CHECK(a.rampTime() == mixMatrix::DEFAULT_RAMP);
    CHECK(a.rampTime() == 10.f);
    CHECK(a.defaultGain() == 1.f);
    CHECK(a.exclusive() == mixMatrix::EXCLUSIVE_OFF);
    CHECK_FALSE(a.ramping());

    mixMatrix b(0, 500, 0.5f);
    CHECK(b.inputs() == 1);
    CHECK(b.outputs() == mixMatrix::MAX_CHANNELS);
    CHECK(b.defaultGain() == 0.5f);
    CHECK(mixMatrix(3, 2, std::numeric_limits<float>::quiet_NaN()).defaultGain() == 1.f);

    mixMatrix c(4, 3);
    for (unsigned int in = 0; in < 4; ++in)
      for (unsigned int out = 0; out < 3; ++out) {
        CHECK(c.gain(in, out) == 0.f);
        CHECK(c.target(in, out) == 0.f);
      }
    CHECK(c.exclusive(7).exclusive() == mixMatrix::EXCLUSIVE_BOTH);
    CHECK(c.exclusive(-2).exclusive() == mixMatrix::EXCLUSIVE_OFF);
    CHECK(c.exclusive(2).exclusive() == mixMatrix::EXCLUSIVE_OUTPUTS);
    CHECK(c.rampTime(-5.f).rampTime() == 0.f);
    CHECK(c.rampTime(25.f).rampTime() == 25.f);
    CHECK(c.rampTime(std::numeric_limits<float>::infinity()).rampTime() == 25.f);
    CHECK(c.defaultGain(0.25f).defaultGain() == 0.25f);
    CHECK(c.defaultGain(std::nanf("")).defaultGain() == 0.25f);
    // Past the last output reads the last.
    CHECK(&c.output(9) == &c.output(2));
    CHECK(c.gain(4, 0) == 0.f);
    CHECK(c.target(0, 3) == 0.f);

    // Nothing connected: every output silent.
    buffer in0(N);
    FillInput(in0, 0);
    buffer* ins[4] = {&in0, &in0, &in0, &in0};
    c(ins);
    for (unsigned int out = 0; out < 3; ++out)
      CHECK(c.output(out).isSilent());
  }

  TEST_CASE("mixMatrix: each output is the sum of its inputs times their gains (#1134)") {
    mixMatrix m(3, 2);
    m.rampTime(0.f);
    m.set(0, 0, 1.f).set(1, 0, 0.5f).set(2, 1, -2.f).set(0, 1, 0.25f);
    CHECK(m.target(1, 0) == 0.5f);
    CHECK(m.target(2, 1) == -2.f);

    buffer a(N);
    buffer b(N);
    buffer c(N);
    FillInput(a, 0);
    FillInput(b, 1);
    FillInput(c, 2);
    buffer* ins[3] = {&a, &b, &c};
    for (int block = 0; block < 2; ++block) {
      buffer& first = m(ins);
      CHECK(&first == &m.output(0));
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        CHECK(m.output(0).getPtr()[i] ==
              doctest::Approx(a.getPtr()[i] + (0.5f * b.getPtr()[i])).epsilon(1e-6));
        CHECK(m.output(1).getPtr()[i] ==
              doctest::Approx((-2.f * c.getPtr()[i]) + (0.25f * a.getPtr()[i])).epsilon(1e-6));
      }
    }
    CHECK(m.gain(1, 0) == 0.5f);
    CHECK_FALSE(m.ramping());

    // A null input is silent; the others still mix.
    buffer* some[3] = {&a, nullptr, nullptr};
    m(some);
    for (unsigned int i = 0; i < N; ++i) {
      CHECK(m.output(0).getPtr()[i] == a.getPtr()[i]);
      CHECK(m.output(1).getPtr()[i] == doctest::Approx(0.25f * a.getPtr()[i]));
    }

    // Disconnecting with a hard ramp: gone from the next sample.
    m.set(0, 0, 0.f);
    m(ins);
    for (unsigned int i = 0; i < N; ++i)
      CHECK(m.output(0).getPtr()[i] == doctest::Approx(0.5f * b.getPtr()[i]).epsilon(1e-6));
  }

  TEST_CASE("mixMatrix: a gain change ramps linearly over the ramp time at the live sample "
            "rate (#1134)") {
    for (const unsigned int rate : {48000u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      mixMatrix m(1, 1);
      const double samples = 10.0 * rate / 1000.0; // the default 10 ms
      buffer dc(N);
      dc = 1.f;
      buffer* ins[1] = {&dc};

      m.connect(0, 0);
      CHECK(m.ramping());
      CHECK(m.target(0, 0) == 1.f);
      CHECK(m.gain(0, 0) == 0.f);
      const unsigned int blocks = static_cast<unsigned int>(samples) / N + 2;
      float previous = 0.f;
      for (unsigned int b = 0; b < blocks; ++b) {
        m(ins);
        for (unsigned int i = 0; i < N; ++i) {
          const unsigned int k = b * N + i;
          CAPTURE(k);
          const float y = m.output(0).getPtr()[i];
          CHECK(y == doctest::Approx(RampAt(0.f, 1.f, samples, k)).epsilon(1e-5));
          CHECK(y >= previous);
          previous = y;
        }
      }
      CHECK_FALSE(m.ramping());
      CHECK(m.gain(0, 0) == 1.f);

      // And back down over the same time.
      m.disconnect(0, 0);
      for (unsigned int b = 0; b < blocks; ++b) {
        m(ins);
        for (unsigned int i = 0; i < N; ++i) {
          const unsigned int k = b * N + i;
          CHECK(m.output(0).getPtr()[i] ==
                doctest::Approx(RampAt(1.f, 0.f, samples, k)).epsilon(1e-5));
        }
      }
      CHECK(m.gain(0, 0) == 0.f);
      CHECK(m.output(0).isSilent());
    }
  }

  TEST_CASE("mixMatrix: follows a sample rate change after construction and during a ramp "
            "(#1134)") {
    TestHelpers::ScopedSampleRate scoped(48000);
    mixMatrix m(1, 1);
    buffer dc(N);
    dc = 1.f;
    buffer* ins[1] = {&dc};

    // Built at 48 kHz, run at 96 kHz: 10 ms is 960 samples.
    YSE::SAMPLERATE = 96000;
    m.connect(0, 0);
    m(ins);
    for (unsigned int i = 0; i < N; ++i)
      CHECK(m.output(0).getPtr()[i] == doctest::Approx((i + 1) / 960.0).epsilon(1e-5));

    // Halfway into the next ramp the rate drops to 48 kHz: the rest of the
    // ramp moves at 1/480 per sample.
    const float reached = m.gain(0, 0);
    CHECK(reached == doctest::Approx(128.0 / 960.0).epsilon(1e-5));
    YSE::SAMPLERATE = 48000;
    m(ins);
    for (unsigned int i = 0; i < N; ++i)
      CHECK(m.output(0).getPtr()[i] == doctest::Approx(reached + ((i + 1) / 480.0)).epsilon(1e-5));
  }

  TEST_CASE("mixMatrix: a change during a ramp starts from the gain reached; the same goal is "
            "ignored; set() takes a ramp time of its own (#1134)") {
    TestHelpers::ScopedSampleRate scoped(48000);
    buffer dc(N);
    dc = 1.f;
    buffer* ins[1] = {&dc};

    mixMatrix m(1, 1);
    m.set(0, 0, 1.f, 1000.f * 512.f / 48000.f); // 512 samples
    m(ins);
    const float reached = m.gain(0, 0);
    CHECK(reached == doctest::Approx(128.0 / 512.0).epsilon(1e-5));

    // The same goal again: the ramp carries on unchanged, even with another
    // ramp time.
    m.set(0, 0, 1.f, 0.f);
    m(ins);
    CHECK(m.output(0).getPtr()[N - 1] == doctest::Approx(256.0 / 512.0).epsilon(1e-5));
    CHECK(m.ramping());

    // A new goal: down to -1 over 256 samples, from 0.5.
    const float from = m.gain(0, 0);
    m.set(0, 0, -1.f, 1000.f * 256.f / 48000.f);
    for (unsigned int b = 0; b < 3; ++b) {
      m(ins);
      for (unsigned int i = 0; i < N; ++i)
        CHECK(m.output(0).getPtr()[i] ==
              doctest::Approx(RampAt(from, -1.f, 256.0, b * N + i)).epsilon(1e-5));
    }
    CHECK(m.gain(0, 0) == -1.f);

    // A ramp time of 0, or under one sample, jumps at the first sample.
    m.set(0, 0, 0.5f, 0.f);
    m(ins);
    CHECK(m.output(0).getPtr()[0] == 0.5f);
    m.set(0, 0, 0.25f, 0.01f);
    m(ins);
    CHECK(m.output(0).getPtr()[0] == 0.25f);
    // A ramp time below 0 is 0; one that is not finite is rampTime().
    m.set(0, 0, 0.75f, -3.f);
    m(ins);
    CHECK(m.output(0).getPtr()[0] == 0.75f);
    m.rampTime(1000.f * 480.f / 48000.f);
    m.set(0, 0, 0.f, std::numeric_limits<float>::quiet_NaN());
    m(ins);
    CHECK(m.output(0).getPtr()[0] == doctest::Approx(0.75f - (0.75f / 480.f)).epsilon(1e-5));
  }

  TEST_CASE("mixMatrix: connect, disconnect and clear; ports out of range and gains that are "
            "not finite are ignored (#1134)") {
    mixMatrix m(2, 3, 0.5f);
    m.rampTime(0.f);
    m.connect(0, 0).connect(0, 2).connect(1, 1);
    CHECK(m.target(0, 0) == 0.5f);
    CHECK(m.target(0, 2) == 0.5f);
    CHECK(m.target(1, 1) == 0.5f);
    CHECK(m.target(1, 0) == 0.f);
    m.disconnect(0, 2);
    CHECK(m.target(0, 2) == 0.f);

    m.set(2, 0, 1.f).set(0, 3, 1.f).set(1, 2, std::numeric_limits<float>::infinity());
    m.set(1, 2, std::nanf(""));
    m.connect(5, 5).disconnect(9, 0);
    for (unsigned int in = 0; in < 2; ++in)
      for (unsigned int out = 0; out < 3; ++out) {
        CAPTURE(in);
        CAPTURE(out);
        const bool on = (in == 0 && out == 0) || (in == 1 && out == 1);
        CHECK(m.target(in, out) == (on ? 0.5f : 0.f));
      }

    // clear ramps every crosspoint out over the ramp time.
    TestHelpers::ScopedSampleRate scoped(48000);
    buffer dc(N);
    dc = 1.f;
    buffer* ins[2] = {&dc, &dc};
    m(ins);
    m.rampTime(1000.f * 256.f / 48000.f);
    m.clear();
    CHECK(m.ramping());
    m(ins);
    CHECK(m.output(0).getPtr()[0] == doctest::Approx(0.5f - (0.5f / 256.f)).epsilon(1e-5));
    CHECK(m.output(1).getPtr()[N - 1] == doctest::Approx(0.25f).epsilon(1e-5));
    m(ins);
    m(ins);
    CHECK_FALSE(m.ramping());
    for (unsigned int out = 0; out < 3; ++out)
      CHECK(m.output(out).isSilent());
  }

  TEST_CASE("mixMatrix: exclusive modes replace the connections a new one would share an "
            "input or an output with (#1134)") {
    // The rule, on its own.
    std::vector<std::pair<unsigned int, unsigned int>> visited;
    const auto collect = [&visited](unsigned int in, unsigned int out) {
      visited.emplace_back(in, out);
    };
    mixMatrix::Exclusions(mixMatrix::EXCLUSIVE_OFF, 1, 1, 3, 3, collect);
    CHECK(visited.empty());
    mixMatrix::Exclusions(mixMatrix::EXCLUSIVE_INPUTS, 1, 1, 3, 3, collect);
    CHECK(visited == std::vector<std::pair<unsigned int, unsigned int>>{{1, 0}, {1, 2}});
    visited.clear();
    mixMatrix::Exclusions(mixMatrix::EXCLUSIVE_OUTPUTS, 1, 1, 3, 3, collect);
    CHECK(visited == std::vector<std::pair<unsigned int, unsigned int>>{{0, 1}, {2, 1}});
    visited.clear();
    mixMatrix::Exclusions(mixMatrix::EXCLUSIVE_BOTH, 0, 2, 2, 3, collect);
    CHECK(visited == std::vector<std::pair<unsigned int, unsigned int>>{{0, 0}, {0, 1}, {1, 2}});

    mixMatrix m(3, 3);
    m.rampTime(0.f);
    m.connect(0, 0).connect(0, 1).connect(1, 1);
    // Turning it on leaves the connections already made alone.
    m.exclusive(mixMatrix::EXCLUSIVE_INPUTS);
    CHECK(m.target(0, 0) == 1.f);
    CHECK(m.target(0, 1) == 1.f);
    // Input 0 to output 2: input 0's other connections go.
    m.connect(0, 2);
    CHECK(m.target(0, 0) == 0.f);
    CHECK(m.target(0, 1) == 0.f);
    CHECK(m.target(0, 2) == 1.f);
    CHECK(m.target(1, 1) == 1.f);
    m.connect(1, 0);
    CHECK(m.target(1, 1) == 0.f);
    CHECK(m.target(1, 0) == 1.f);
    // A disconnection disconnects nothing else.
    m.connect(1, 1);
    m.set(0, 1, 0.f);
    CHECK(m.target(0, 2) == 1.f);
    CHECK(m.target(1, 1) == 1.f);
    CHECK(m.target(1, 0) == 0.f);

    m.exclusive(mixMatrix::EXCLUSIVE_OUTPUTS);
    m.connect(2, 2);
    CHECK(m.target(0, 2) == 0.f);
    CHECK(m.target(2, 2) == 1.f);
    m.connect(2, 1);
    CHECK(m.target(1, 1) == 0.f);
    CHECK(m.target(2, 1) == 1.f);
    CHECK(m.target(2, 2) == 1.f); // input 2 may reach two outputs

    m.exclusive(mixMatrix::EXCLUSIVE_BOTH);
    m.connect(0, 1);
    CHECK(m.target(2, 1) == 0.f);
    CHECK(m.target(0, 1) == 1.f);
    m.connect(0, 2);
    CHECK(m.target(0, 1) == 0.f);
    CHECK(m.target(2, 2) == 0.f);
    CHECK(m.target(0, 2) == 1.f);

    // With a ramp, the old connection fades out while the new one fades in:
    // an output switching inputs crossfades.
    TestHelpers::ScopedSampleRate scoped(48000);
    mixMatrix x(2, 1);
    x.exclusive(mixMatrix::EXCLUSIVE_OUTPUTS).rampTime(1000.f * 256.f / 48000.f);
    buffer one(N);
    buffer two(N);
    one = 1.f;
    two = 2.f;
    buffer* ins[2] = {&one, &two};
    x.set(0, 0, 1.f, 0.f);
    x(ins);
    x.connect(1, 0);
    for (unsigned int b = 0; b < 2; ++b) {
      x(ins);
      for (unsigned int i = 0; i < N; ++i) {
        const unsigned int k = b * N + i;
        const float expected = RampAt(1.f, 0.f, 256.0, k) + (2.f * RampAt(0.f, 1.f, 256.0, k));
        CHECK(x.output(0).getPtr()[i] == doctest::Approx(expected).epsilon(1e-5));
      }
    }
    CHECK(x.output(0).getPtr()[N - 1] == doctest::Approx(2.f));
  }

  TEST_CASE("mixMatrix: block lengths (#1134)") {
    mixMatrix m(2, 2);
    m.rampTime(0.f).connect(0, 0).connect(1, 1);
    buffer shortIn(32);
    buffer full(N);
    shortIn = 1.f;
    full = 2.f;
    buffer* ins[2] = {&shortIn, &full};
    m(ins);
    CHECK(m.output(0).getLength() == 32);
    CHECK(m.output(1).getLength() == 32);
    CHECK(m.output(1).getPtr()[31] == 2.f);

    buffer* none[2] = {nullptr, nullptr};
    m(none);
    CHECK(m.output(0).getLength() == N);
    CHECK(m.output(0).isSilent());

    buffer* back[2] = {&full, &full};
    m(back);
    CHECK(m.output(0).getLength() == N);
    CHECK(m.output(0).getPtr()[N - 1] == 2.f);
  }

  TEST_CASE("mixMatrix: changes and blocks allocate nothing (#1134)") {
    mixMatrix m(8, 8);
    buffer in(N);
    in = 0.5f;
    std::vector<buffer*> ins(8, &in);
    ins[3] = nullptr;
    buffer shortIn(16);
    shortIn = 0.25f;
    m(ins.data());
    {
      TestHelpers::ProbeScope probe;
      for (unsigned int i = 0; i < 8; ++i)
        m.connect(i, 7 - i);
      m.set(2, 2, 0.5f, 3.f).exclusive(mixMatrix::EXCLUSIVE_BOTH).connect(0, 0);
      m(ins.data());
      m.rampTime(0.f).clear();
      m(ins.data());
      ins[0] = &shortIn;
      m(ins.data());
      ins[0] = &in;
      m(ins.data());
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
