// Tests for YSE::DSP::signalSelector (YseEngine/dsp/routing/signalSelector.hpp),
// the signal routing behind ~gate and ~selector (issue #1133): the channel a
// number or a control value opens, next() wrapping, hard switching for select()
// and route(), per-sample control signals, step mode, the linear crossfade in
// closed form at the live sample rate, a switch during a fade, block lengths,
// and allocation freedom.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include "dsp/routing/signalSelector.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::signalSelector;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // A signal that differs per channel and per sample, so a wrong channel or
  // a wrong sample shows.
  void FillChannel(buffer& b, unsigned int channel) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = static_cast<float>(channel * 1000 + i);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("signalSelector: channel numbers, clamping and next() (#1133)") {
    CHECK(signalSelector::Channel(0.f, 4) == 0);
    CHECK(signalSelector::Channel(0.999f, 4) == 0);
    CHECK(signalSelector::Channel(-3.f, 4) == 0);
    CHECK(signalSelector::Channel(std::nanf(""), 4) == 0);
    CHECK(signalSelector::Channel(1.f, 4) == 1);
    CHECK(signalSelector::Channel(1.999f, 4) == 1);
    CHECK(signalSelector::Channel(3.5f, 4) == 3);
    CHECK(signalSelector::Channel(4.f, 4) == 4);
    CHECK(signalSelector::Channel(1e9f, 4) == 4);
    CHECK(signalSelector::Channel(std::numeric_limits<float>::infinity(), 4) == 4);

    CHECK(signalSelector::Next(0, 3) == 1);
    CHECK(signalSelector::Next(-2, 3) == 1);
    CHECK(signalSelector::Next(1, 3) == 2);
    CHECK(signalSelector::Next(2, 3) == 3);
    CHECK(signalSelector::Next(3, 3) == 1);
    CHECK(signalSelector::Next(9, 3) == 1);
    CHECK(signalSelector::Next(1, 1) == 1);

    signalSelector a;
    CHECK(a.channels() == signalSelector::DEFAULT_CHANNELS);
    CHECK(a.open() == 0);
    CHECK(a.rampTime() == 0.f);
    CHECK_FALSE(a.stepMode());
    CHECK(signalSelector(0).channels() == 1);
    CHECK(signalSelector(500).channels() == signalSelector::MAX_CHANNELS);

    signalSelector b(3, 2);
    CHECK(b.open() == 2);
    CHECK(b.gain(2) == 1.f);
    CHECK(b.gain(1) == 0.f);
    CHECK(b.gain(0) == 0.f);
    CHECK(b.gain(4) == 0.f);
    CHECK(signalSelector(3, 7).open() == 3);
    CHECK(signalSelector(3, -1).open() == 0);
    b.open(9);
    CHECK(b.open() == 3);
    b.open(-4);
    CHECK(b.open() == 0);
    b.next();
    CHECK(b.open() == 1);
    b.next().next();
    CHECK(b.open() == 3);
    b.next();
    CHECK(b.open() == 1);

    b.rampTime(5.f);
    CHECK(b.rampTime() == 5.f);
    b.rampTime(std::nanf(""));
    CHECK(b.rampTime() == 5.f);
    b.rampTime(-2.f);
    CHECK(b.rampTime() == 0.f);
    CHECK(b.stepMode(true).stepMode());
  }

  TEST_CASE("signalSelector: select() passes the open input exactly, or silence (#1133)") {
    buffer in1(N);
    buffer in2(N);
    buffer in3(N);
    FillChannel(in1, 1);
    FillChannel(in2, 2);
    FillChannel(in3, 3);
    buffer* inputs[3] = {&in1, &in2, &in3};

    signalSelector fn(3);
    buffer& closed = fn.select(inputs);
    CHECK(closed.getLength() == N);
    CHECK(closed.isSilent());
    CHECK(&closed == &fn.output(0));

    fn.open(2);
    CHECK(TestHelpers::buffersNearlyEqual(fn.select(inputs), in2, 0.f));
    fn.open(3);
    CHECK(TestHelpers::buffersNearlyEqual(fn.select(inputs), in3, 0.f));
    fn.next();
    CHECK(TestHelpers::buffersNearlyEqual(fn.select(inputs), in1, 0.f));
    fn.open(0);
    CHECK(fn.select(inputs).isSilent());

    // A null input is silent.
    buffer* sparse[3] = {&in1, nullptr, &in3};
    fn.open(2);
    CHECK(fn.select(sparse).isSilent());
    fn.open(3);
    CHECK(TestHelpers::buffersNearlyEqual(fn.select(sparse), in3, 0.f));
  }

  TEST_CASE("signalSelector: route() sends the input to the open output, silence to the "
            "others (#1133)") {
    buffer in(N);
    FillChannel(in, 7);
    signalSelector fn(3, 1);
    buffer& first = fn.route(in);
    CHECK(&first == &fn.output(0));
    CHECK(TestHelpers::buffersNearlyEqual(fn.output(0), in, 0.f));
    CHECK(fn.output(1).isSilent());
    CHECK(fn.output(2).isSilent());

    fn.open(3);
    fn.route(in);
    CHECK(fn.output(0).isSilent());
    CHECK(fn.output(1).isSilent());
    CHECK(TestHelpers::buffersNearlyEqual(fn.output(2), in, 0.f));
    // An index past the last channel reads the last.
    CHECK(&fn.output(9) == &fn.output(2));

    fn.open(0);
    fn.route(in);
    for (unsigned int c = 0; c < 3; ++c)
      CHECK(fn.output(c).isSilent());
  }

  TEST_CASE("signalSelector: a control signal chooses the channel per sample (#1133)") {
    buffer in1(N);
    buffer in2(N);
    buffer in3(N);
    FillChannel(in1, 1);
    FillChannel(in2, 2);
    FillChannel(in3, 3);
    buffer* inputs[3] = {&in1, &in2, &in3};

    const float values[] = {0.f, 0.5f, 1.f, 1.9f, 2.f, 3.5f, 10.f, -1.f, std::nanf(""), 2.2f};
    const int channels[] = {0, 0, 1, 1, 2, 3, 3, 0, 0, 2};
    constexpr unsigned int COUNT = sizeof(values) / sizeof(values[0]);
    buffer control(N);
    for (unsigned int i = 0; i < N; ++i)
      control.getPtr()[i] = values[i % COUNT];

    signalSelector selector(3);
    // The control signal wins over the number.
    selector.open(1);
    buffer& out = selector.select(inputs, control);
    signalSelector gate(3);
    buffer routed(N);
    FillChannel(routed, 5);
    gate.route(routed, control);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      const int channel = channels[i % COUNT];
      const float expected = channel == 0 ? 0.f : inputs[channel - 1]->getPtr()[i];
      CHECK(out.getPtr()[i] == expected);
      for (int c = 1; c <= 3; ++c)
        CHECK(gate.output(c - 1).getPtr()[i] == (c == channel ? routed.getPtr()[i] : 0.f));
    }
    // The open channel is the last sample's.
    CHECK(selector.open() == channels[(N - 1) % COUNT]);
  }

  TEST_CASE("signalSelector: step mode opens the next channel on each whole-number change "
            "(#1133)") {
    buffer in1(N);
    buffer in2(N);
    buffer in3(N);
    in1 = 1.f;
    in2 = 2.f;
    in3 = 3.f;
    buffer* inputs[3] = {&in1, &in2, &in3};

    // Each value is held for 4 samples. Changes of the whole part step
    // (0 -> 1 opens 1, 1.5 stays, 2 opens 2, 7 opens 3, 7.9 stays, 3 wraps
    // to 1); a whole part of 0 or less closes; leaving it steps from closed
    // to 1.
    const float values[] = {0.f, 1.f, 1.5f, 2.f, 7.f, 7.9f, 3.f, 0.4f, -2.f, 5.f, 6.f};
    const float expected[] = {0.f, 1.f, 1.f, 2.f, 3.f, 3.f, 1.f, 0.f, 0.f, 1.f, 2.f};
    constexpr unsigned int COUNT = sizeof(values) / sizeof(values[0]);
    buffer control(COUNT * 4);
    for (unsigned int i = 0; i < COUNT * 4; ++i)
      control.getPtr()[i] = values[i / 4];

    signalSelector fn(3);
    fn.stepMode(true);
    buffer& out = fn.select(inputs, control);
    REQUIRE(out.getLength() == COUNT * 4);
    for (unsigned int i = 0; i < COUNT * 4; ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == expected[i / 4]);
    }

    // The gate steps the same way.
    signalSelector gate(3);
    gate.stepMode(true);
    buffer one(COUNT * 4);
    one = 1.f;
    gate.route(one, control);
    for (unsigned int i = 0; i < COUNT * 4; ++i) {
      CAPTURE(i);
      const auto channel = static_cast<unsigned int>(expected[i / 4]);
      for (unsigned int c = 1; c <= 3; ++c)
        CHECK(gate.output(c - 1).getPtr()[i] == (c == channel ? 1.f : 0.f));
    }
  }

  TEST_CASE("signalSelector: a ramp time crossfades linearly at the live sample rate "
            "(#1133)") {
    const auto check = [](unsigned int rate) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      // 1 ms of crossfade.
      const unsigned int samples = rate / 1000;
      buffer one(N);
      buffer zero(N);
      one = 1.f;
      zero = 0.f;
      buffer* inputs[2] = {&one, &zero};

      signalSelector selector(2, 1);
      selector.rampTime(1.f);
      signalSelector gate(2, 1);
      gate.rampTime(1.f);
      // Steady before the switch: no fade at the start.
      CHECK(TestHelpers::buffersNearlyEqual(selector.select(inputs), one, 0.f));
      selector.open(2);
      gate.open(2);
      buffer& out = selector.select(inputs);
      gate.route(one);
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        // Input 1 fades out, input 2 (silent) in: the output is input 1's
        // gain, falling by 1/samples per sample from the switch on.
        const float fading = i + 1 >= samples ? 0.f : 1.f - static_cast<float>(i + 1) / samples;
        CHECK(out.getPtr()[i] == doctest::Approx(fading).epsilon(1e-5));
        CHECK(gate.output(0).getPtr()[i] == doctest::Approx(fading).epsilon(1e-5));
        CHECK(gate.output(1).getPtr()[i] == doctest::Approx(1.f - fading).epsilon(1e-5));
      }
      CHECK(selector.gain(1) == 0.f);
      CHECK(selector.gain(2) == 1.f);
    };
    check(48000);
    check(96000);

    // The rate is read at each block, not in the constructor.
    signalSelector fn(2, 1);
    fn.rampTime(2.f);
    buffer one(N);
    one = 1.f;
    buffer* inputs[2] = {&one, nullptr};
    {
      TestHelpers::ScopedSampleRate scoped(44100);
      fn.select(inputs);
    }
    TestHelpers::ScopedSampleRate scoped(32000);
    fn.open(2);
    buffer& out = fn.select(inputs);
    // 2 ms at 32 kHz: 64 samples.
    CHECK(out.getPtr()[31] == doctest::Approx(0.5f).epsilon(1e-5));
    CHECK(out.getPtr()[62] == doctest::Approx(1.f / 64.f).epsilon(1e-3));
    CHECK(out.getPtr()[63] == 0.f);

    // Under one sample is a hard switch.
    signalSelector hard(2, 1);
    hard.rampTime(0.01f);
    hard.open(2);
    CHECK(hard.select(inputs).isSilent());
  }

  TEST_CASE("signalSelector: a switch during a fade carries on from the gains reached "
            "(#1133)") {
    // 1 ms at 64 kHz: 64 samples, a step of 1/64, exact in a float.
    TestHelpers::ScopedSampleRate scoped(64000);
    signalSelector fn(3, 1);
    fn.rampTime(1.f);
    buffer in(32);
    in = 1.f;
    fn.open(2);
    fn.route(in);
    CHECK(fn.gain(1) == 0.5f);
    CHECK(fn.gain(2) == 0.5f);
    fn.open(3);
    fn.route(in);
    // Both fading out from 0.5 reach 0 after 32 samples; 3 is halfway in.
    CHECK(fn.gain(1) == 0.f);
    CHECK(fn.gain(2) == 0.f);
    CHECK(fn.gain(3) == 0.5f);
    for (unsigned int i = 0; i < 32; ++i)
      CHECK(fn.output(2).getPtr()[i] == static_cast<float>(i + 1) / 64.f);
    fn.route(in);
    CHECK(fn.gain(3) == 1.f);
    for (unsigned int i = 0; i < 32; ++i)
      CHECK(fn.output(2).getPtr()[i] == static_cast<float>(i + 33) / 64.f);
    // Back to steady: the next block is the plain copy.
    fn.route(in);
    CHECK(fn.output(0).isSilent());
    CHECK(TestHelpers::buffersNearlyEqual(fn.output(2), in, 0.f));
  }

  TEST_CASE("signalSelector: block lengths follow the inputs, at most STANDARD_BUFFERSIZE "
            "(#1133)") {
    buffer shortIn(100);
    buffer longIn(N * 2);
    buffer control(60);
    shortIn = 1.f;
    longIn = 2.f;
    control = 2.f;
    buffer* inputs[2] = {&shortIn, &longIn};
    signalSelector fn(2, 2);
    CHECK(fn.select(inputs).getLength() == 100);
    CHECK(fn.select(inputs, control).getLength() == 60);
    buffer* onlyLong[2] = {nullptr, &longIn};
    CHECK(fn.select(onlyLong).getLength() == N);
    fn.route(longIn);
    CHECK(fn.output(0).getLength() == N);
    CHECK(fn.output(1).getLength() == N);
    fn.route(shortIn, control);
    CHECK(fn.output(1).getLength() == 60);
  }

  TEST_CASE("signalSelector: processing allocates nothing (#1133)") {
    buffer in(N);
    buffer other(N);
    buffer shorter(N / 2);
    buffer control(N);
    in = 0.5f;
    other = -0.5f;
    shorter = 0.25f;
    for (unsigned int i = 0; i < N; ++i)
      control.getPtr()[i] = static_cast<float>(i % 5);
    buffer* inputs[3] = {&in, &other, nullptr};
    signalSelector fn(3, 1);
    fn.select(inputs);
    {
      TestHelpers::ProbeScope probe;
      fn.rampTime(3.f).open(2);
      fn.select(inputs);
      fn.next();
      fn.select(inputs, control);
      fn.stepMode(true);
      fn.select(inputs, control);
      fn.route(in);
      fn.route(shorter);
      fn.route(in, control);
      fn.stepMode(false).rampTime(0.f).open(1);
      fn.route(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
