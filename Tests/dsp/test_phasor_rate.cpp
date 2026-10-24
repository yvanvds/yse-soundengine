// Tests for YSE::DSP::phasorRate (YseEngine/dsp/generators/phasorRate.hpp),
// issue #1073. The input is a DSP::phasor at 375 Hz and 48 kHz: exactly 128
// samples per cycle, so sample n reads (n mod 128) / 128 and the in-phase
// output for multiplier m is the closed form frac(n / (128 m)).

#include <doctest/doctest.h>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorRate.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorRate;
using rateSync = YSE::DSP::phasorRate::syncMode;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PERIOD = 128.0;

  // `samples` samples of a 375 Hz DSP::phasor (128 samples per cycle).
  std::vector<float> Ramp(unsigned int samples) {
    TestHelpers::ScopedSampleRate rate(48000);
    phasor source;
    std::vector<float> result;
    while (result.size() < samples) {
      buffer& out = source(375.f, N);
      for (unsigned int i = 0; i < N && result.size() < samples; ++i)
        result.push_back(out.getPtr()[i]);
    }
    return result;
  }

  double Frac(double x) {
    return x - std::floor(x);
  }

  // Distance on the 0..1 circle, so 0.9999999 and 0 are neighbours.
  double Circular(double a, double b) {
    const double d = Frac(a - b);
    return d < 0.5 ? d : 1.0 - d;
  }

  // In phase with the 128-sample input for multiplier m.
  double InPhase(unsigned int n, double m) {
    return Frac(static_cast<double>(n) / (PERIOD * m));
  }

  // Sample by sample through process(), calling `before(n)` ahead of
  // sample n so a test can change the class at an exact sample.
  std::vector<float> Process(phasorRate& fn, const std::vector<float>& in,
                             const std::function<void(unsigned int)>& before = nullptr) {
    std::vector<float> out;
    for (unsigned int n = 0; n < in.size(); ++n) {
      if (before) before(n);
      out.push_back(fn.process(in[n]));
    }
    return out;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("phasorRate: defaults and setters (#1073)") {
    phasorRate fn;
    CHECK(fn.multiplier() == 1.f);
    CHECK(fn.sync() == rateSync::off);
    CHECK_FALSE(fn.oneshot());
    CHECK_FALSE(fn.stopped());
    fn.multiplier(0.25f).sync(rateSync::lock).oneshot(true);
    CHECK(fn.multiplier() == 0.25f);
    CHECK(fn.sync() == rateSync::lock);
    CHECK(fn.oneshot());
    // 0 and non-finite multipliers are ignored.
    fn.multiplier(0.f);
    fn.multiplier(std::numeric_limits<float>::quiet_NaN());
    fn.multiplier(std::numeric_limits<float>::infinity());
    CHECK(fn.multiplier() == 0.25f);
    CHECK(static_cast<int>(rateSync::cycle) == 0);
    CHECK(static_cast<int>(rateSync::lock) == 1);
    CHECK(static_cast<int>(rateSync::off) == 2);
  }

  TEST_CASE("phasorRate: a steady multiplier gives the in-phase ramp frac(n / (128 m)) in every "
            "sync mode (#1073)") {
    const std::vector<float> in = Ramp(8 * N);
    for (const float m : {0.25f, 0.5f, 1.f, 2.f, 3.f, 0.3f, -0.5f, -2.f}) {
      for (const rateSync mode : {rateSync::off, rateSync::lock, rateSync::cycle}) {
        CAPTURE(m);
        CAPTURE(static_cast<int>(mode));
        phasorRate fn;
        fn.multiplier(m).sync(mode);
        buffer block(N);
        for (unsigned int first = 0; first < in.size(); first += N) {
          for (unsigned int i = 0; i < N; ++i)
            block.getPtr()[i] = in[first + i];
          buffer& out = fn(block);
          REQUIRE(out.getLength() == N);
          for (unsigned int i = 0; i < N; ++i) {
            const unsigned int n = first + i;
            CAPTURE(n);
            const float y = out.getPtr()[i];
            CHECK(y >= 0.f);
            CHECK(y < 1.f);
            CHECK(Circular(y, InPhase(n, m)) < 1e-6);
          }
        }
      }
    }
  }

  TEST_CASE("phasorRate: 0.25 makes four ramps per input ramp, 2 one ramp per two (#1073)") {
    const std::vector<float> in = Ramp(1024); // 8 input cycles
    const auto wraps = [&](float m) {
      phasorRate fn;
      fn.multiplier(m);
      const std::vector<float> out = Process(fn, in);
      int count = 0;
      for (unsigned int n = 1; n < out.size(); ++n)
        if (out[n] < out[n - 1]) count++;
      return count;
    };
    CHECK(wraps(0.25f) == 31); // the 32nd falls on sample 1024
    CHECK(wraps(1.f) == 7); // the 8th input wrap is at sample 1024
    CHECK(wraps(2.f) == 3);
  }

  TEST_CASE("phasorRate: a new multiplier - off runs on, lock jumps in phase, cycle jumps at the "
            "next input wrap (#1073)") {
    const std::vector<float> in = Ramp(1024);
    // 1 for samples 0..199, then 3.
    const auto run = [&](rateSync mode) {
      phasorRate fn;
      fn.sync(mode);
      return Process(fn, in, [&](unsigned int n) {
        if (n == 200) fn.multiplier(3.f);
      });
    };

    const std::vector<float> off = run(rateSync::off);
    for (unsigned int n = 0; n < 200; ++n)
      CHECK(Circular(off[n], InPhase(n, 1.0)) < 1e-6);
    for (unsigned int n = 200; n < off.size(); ++n) {
      CAPTURE(n);
      CHECK(Circular(off[n], off[199] + (n - 199) / (PERIOD * 3.0)) < 1e-6);
    }
    // Out of phase from there on: at sample 200 the in-phase value is 200/384.
    CHECK(Circular(off[200], InPhase(200, 3.0)) > 0.01);

    const std::vector<float> lock = run(rateSync::lock);
    for (unsigned int n = 0; n < 200; ++n)
      CHECK(Circular(lock[n], InPhase(n, 1.0)) < 1e-6);
    for (unsigned int n = 200; n < lock.size(); ++n) {
      CAPTURE(n);
      CHECK(Circular(lock[n], InPhase(n, 3.0)) < 1e-6);
    }

    const std::vector<float> cycle = run(rateSync::cycle);
    for (unsigned int n = 200; n < 256; ++n) {
      CAPTURE(n);
      CHECK(Circular(cycle[n], cycle[199] + (n - 199) / (PERIOD * 3.0)) < 1e-6);
    }
    for (unsigned int n = 256; n < cycle.size(); ++n) { // input wraps at 256
      CAPTURE(n);
      CHECK(Circular(cycle[n], InPhase(n, 3.0)) < 1e-6);
    }
  }

  TEST_CASE("phasorRate: cycle mode brings a jump back in phase at every input wrap (#1073)") {
    const std::vector<float> in = Ramp(512);
    phasorRate fn;
    fn.multiplier(0.5f).sync(rateSync::cycle);
    const std::vector<float> out = Process(fn, in, [&](unsigned int n) {
      if (n == 40) fn.jump(0.9f);
    });
    CHECK(out[40] == 0.9f);
    CHECK(Circular(out[100], 0.9 + 60.0 / 64.0) < 1e-6);
    for (unsigned int n = 128; n < out.size(); ++n) {
      CAPTURE(n);
      CHECK(Circular(out[n], InPhase(n, 0.5)) < 1e-6);
    }
  }

  TEST_CASE("phasorRate: jump now and jump at an input phase (goto) (#1073)") {
    const std::vector<float> in = Ramp(512);
    phasorRate now;
    std::vector<float> out = Process(now, in, [&](unsigned int n) {
      if (n == 50) now.jump(0.9f);
    });
    CHECK(out[49] == doctest::Approx(49.0 / PERIOD));
    CHECK(out[50] == 0.9f);
    CHECK(Circular(out[51], 0.9 + 1.0 / PERIOD) < 1e-6);
    CHECK(Circular(out[70], 0.9 + 20.0 / PERIOD) < 1e-6);

    // goto 1.0 0.5: 1.0 at the halfway point of the input ramp.
    phasorRate at;
    at.multiplier(0.5f);
    out = Process(at, in, [&](unsigned int n) {
      if (n == 10) at.jump(1.f, 0.5f);
    });
    for (unsigned int n = 0; n < 64; ++n)
      CHECK(Circular(out[n], InPhase(n, 0.5)) < 1e-6);
    CHECK(out[64] == 1.f);
    CHECK(out[65] == doctest::Approx(1.0 / 64.0));
    CHECK(out[100] == doctest::Approx(36.0 / 64.0));

    // At 0: on the input wrap.
    phasorRate wrap;
    out = Process(wrap, in, [&](unsigned int n) {
      if (n == 130) wrap.jump(0.25f, 0.f);
    });
    CHECK(Circular(out[255], 255.0 / PERIOD) < 1e-6);
    CHECK(out[256] == 0.25f);
    CHECK(out[257] == doctest::Approx(0.25 + 1.0 / PERIOD));

    // A later jump replaces one still waiting; a non-finite value is ignored.
    phasorRate replace;
    out = Process(replace, in, [&](unsigned int n) {
      if (n == 10) replace.jump(0.75f, 0.9f).jump(0.5f, 0.25f);
      if (n == 100) replace.jump(std::numeric_limits<float>::quiet_NaN());
    });
    CHECK(out[32] == 0.5f);
    CHECK(Circular(out[115], 0.5 + 83.0 / PERIOD) < 1e-6);
  }

  TEST_CASE("phasorRate: reset restarts the wrap count and the output in phase on the next "
            "input wrap (#1073)") {
    const std::vector<float> in = Ramp(1024);
    phasorRate fn;
    fn.multiplier(3.f).sync(rateSync::lock);
    const std::vector<float> out = Process(fn, in, [&](unsigned int n) {
      if (n == 20) fn.jump(0.7f);
      if (n == 300) fn.reset();
    });
    CHECK(out[20] == 0.7f);
    CHECK(Circular(out[383], 0.7 + 363.0 / 384.0) < 1e-6);
    // Input wrap at 384: the count restarts there.
    for (unsigned int n = 384; n < out.size(); ++n) {
      CAPTURE(n);
      CHECK(Circular(out[n], InPhase(n - 384, 3.0)) < 1e-6);
    }
    CHECK(out[384] == 0.f);
  }

  TEST_CASE("phasorRate: oneshot holds the end of one ramp until a jump or reset (#1073)") {
    const std::vector<float> in = Ramp(1024);
    phasorRate fn;
    fn.multiplier(0.5f).oneshot(true);
    std::vector<float> out = Process(fn, in, [&](unsigned int n) {
      if (n == 300) fn.jump(0.f);
      if (n == 600) fn.reset();
    });
    for (unsigned int n = 0; n < 64; ++n)
      CHECK(out[n] == doctest::Approx(n / 64.0));
    for (unsigned int n = 64; n < 300; ++n)
      CHECK(out[n] == 1.f);
    CHECK(out[300] == 0.f);
    CHECK(out[363] == doctest::Approx(63.0 / 64.0));
    for (unsigned int n = 364; n < 640; ++n)
      CHECK(out[n] == 1.f);
    // The reset waits for the input wrap at 640, then runs one more ramp.
    CHECK(out[640] == 0.f);
    CHECK(out[650] == doctest::Approx(10.0 / 64.0));
    CHECK(out[704] == 1.f);
    CHECK(fn.stopped());

    // Backwards it stops at 0. From 0 the first step already crosses it, so
    // start the ramp at its top.
    phasorRate back;
    back.multiplier(-1.f).oneshot(true);
    out = Process(back, in, [&](unsigned int n) {
      if (n == 0) back.jump(1.f);
    });
    CHECK(out[0] == 1.f);
    CHECK(out[1] == doctest::Approx(127.0 / 128.0));
    CHECK(out[2] == doctest::Approx(126.0 / 128.0));
    CHECK(out[128] == 0.f);
    CHECK(out[129] == 0.f);
    CHECK(out[500] == 0.f);
    CHECK(back.stopped());

    // Turning oneshot off restarts the ramp from its end.
    phasorRate off;
    off.oneshot(true);
    out = Process(off, in, [&](unsigned int n) {
      if (n == 200) off.oneshot(false);
    });
    CHECK(out[150] == 1.f);
    CHECK(out[200] == doctest::Approx(1.0 / PERIOD));
    CHECK_FALSE(off.stopped());
  }

  TEST_CASE("phasorRate: the multiplier buffer is read per sample; 0 and non-finite samples "
            "keep the last multiplier (#1073)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<float> in = Ramp(4 * N);
    phasorRate perSample;
    perSample.sync(rateSync::lock);
    buffer block(N);
    buffer m(N);
    for (unsigned int first = 0; first < in.size(); first += N) {
      for (unsigned int i = 0; i < N; ++i) {
        const unsigned int n = first + i;
        block.getPtr()[i] = in[n];
        m.getPtr()[i] = n == 7 ? 0.f : (n == 9 ? nan : (n < 300 ? 0.5f : 2.f));
      }
      buffer& out = perSample(block, m);
      for (unsigned int i = 0; i < N; ++i) {
        const unsigned int n = first + i;
        CAPTURE(n);
        CHECK(Circular(out.getPtr()[i], InPhase(n, n < 300 ? 0.5 : 2.0)) < 1e-6);
      }
    }
    CHECK(perSample.multiplier() == 1.f); // the setter's value is untouched

    // The shorter of the two buffers sets the length.
    phasorRate shortFn;
    buffer half(N / 2);
    half = 2.f;
    CHECK(shortFn(block, half).getLength() == N / 2);
  }

  TEST_CASE("phasorRate: a non-finite input sample holds the ramp; the sample rate plays no "
            "part (#1073)") {
    std::vector<float> in = Ramp(512);
    in[100] = std::numeric_limits<float>::quiet_NaN();
    in[101] = std::numeric_limits<float>::infinity();
    phasorRate fn;
    std::vector<float> out = Process(fn, in);
    CHECK(out[100] == out[99]);
    CHECK(out[101] == out[99]);
    // Sample 102 catches up: the step from 99 to 102 is three samples.
    CHECK(out[102] == doctest::Approx(102.0 / PERIOD));
    for (float y : out)
      CHECK(std::isfinite(y));

    const std::vector<float> clean = Ramp(512);
    TestHelpers::ScopedSampleRate rate(48000);
    phasorRate a;
    a.multiplier(0.3f);
    const std::vector<float> at48 = Process(a, clean);
    YSE::SAMPLERATE = 96000;
    phasorRate b;
    b.multiplier(0.3f);
    const bool same = Process(b, clean) == at48;
    CHECK(same);
  }

  TEST_CASE("phasorRate: processing, setters, jumps and resets allocate nothing (#1073)") {
    phasorRate fn;
    buffer in(N);
    buffer m(N);
    in = 0.25f;
    m = 2.f;
    fn(in);
    TestHelpers::ProbeScope probe;
    fn.multiplier(0.5f).sync(rateSync::cycle).oneshot(true);
    fn.jump(0.5f).jump(0.2f, 0.5f).reset();
    fn(in);
    fn(in, m);
    (void)fn.process(0.5f);
    (void)fn.process(0.5f, 3.f);
    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

} // TEST_SUITE("dsp")
