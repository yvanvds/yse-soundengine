// Tests for YSE::DSP::waveReader (YseEngine/dsp/sampling/waveReader.hpp),
// issue #1146: a buffer region read as one cycle at a 0 to 1 phase, what
// ~wave and ~2d.wave do. The phase across the region and its wrap, the
// region in ms at the buffer's rate, every interpolation mode against its
// closed form, a sine table against sin(), rows and their crossfade,
// channels, silence, block lengths, the sample rate, and allocation-free
// blocks. The view is a sampleBufferReader's, tested in test_sampleBuffer.cpp.

#include <doctest/doctest.h>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/sampling/sampleBuffer.hpp"
#include "dsp/sampling/waveReader.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::buffer;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferReader;
using YSE::DSP::sampleView;
using YSE::DSP::waveReader;
using interp = YSE::DSP::waveReader::interpolation;

namespace {

  constexpr double TWO_PI = 6.283185307179586;

  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

  // Frame n of channel c is (c + 1) * 10 * n, at `rate` Hz (1000 by
  // default: one frame a millisecond).
  std::shared_ptr<sampleBuffer> Tens(UInt frames, UInt channels = 1, float rate = 1000.f) {
    auto b = sampleBuffer::create(channels, frames, rate);
    for (UInt c = 0; c < channels; ++c) {
      std::vector<float> v(frames);
      for (UInt n = 0; n < frames; ++n)
        v[n] = static_cast<float>((c + 1) * 10 * n);
      b->setSamples(c, 0, v.data(), frames);
    }
    return b;
  }

  std::shared_ptr<sampleBuffer> Values(std::initializer_list<float> values, float rate = 1000.f) {
    auto b = sampleBuffer::create(1, static_cast<UInt>(values.size()), rate);
    std::vector<float> v(values);
    b->setSamples(0, 0, v.data(), static_cast<UInt>(v.size()));
    return b;
  }

  buffer Signal(std::initializer_list<float> values) {
    buffer out(static_cast<UInt>(values.size()));
    UInt i = 0;
    for (float v : values)
      out.getPtr()[i++] = v;
    return out;
  }

  buffer Constant(UInt length, float value) {
    buffer out(length);
    for (UInt i = 0; i < length; ++i)
      out.getPtr()[i] = value;
    return out;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("waveReader: the phase scans the whole buffer and wraps (#1146)") {
    auto b = Tens(8);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    waveReader w;
    CHECK(w.channels() == 1);
    CHECK(w.interp() == interp::LINEAR);
    CHECK(w.rows() == 1);
    // 8 frames, one cycle: phase p reads position 8p.
    CHECK(w.read(view, 0, 0.f) == 0.f);
    CHECK(w.read(view, 0, 0.125f) == 10.f);
    CHECK(w.read(view, 0, 0.5f) == 40.f);
    CHECK(w.read(view, 0, 0.5625f) == doctest::Approx(45.f)); // between 4 and 5
    // The last frame leads back into the first: 7.5 is between 70 and 0.
    CHECK(w.read(view, 0, 0.9375f) == doctest::Approx(35.f));
    // Wrapped: 1 is 0, 1.25 is 0.25, -0.25 is 0.75.
    CHECK(w.read(view, 0, 1.f) == 0.f);
    CHECK(w.read(view, 0, 1.25f) == 20.f);
    CHECK(w.read(view, 0, -0.25f) == 60.f);
    CHECK(w.read(view, 0, 3.5f) == 40.f);

    buffer phase = Signal({0.f, 0.25f, 0.5f, 0.75f, 1.f});
    buffer& out = w(view, phase);
    REQUIRE(out.getLength() == 5);
    CHECK(out.getPtr()[0] == 0.f);
    CHECK(out.getPtr()[1] == 20.f);
    CHECK(out.getPtr()[2] == 40.f);
    CHECK(out.getPtr()[3] == 60.f);
    CHECK(out.getPtr()[4] == 0.f);
    CHECK(&out == &w.out(0));
  }

  TEST_CASE("waveReader: the region in ms, at the buffer's rate (#1146)") {
    auto b = Tens(20); // 1000 Hz: frame n is n ms
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    waveReader w;
    w.start(4.f).end(8.f); // frames 4 .. 7
    CHECK(w.start() == 4.f);
    CHECK(w.end() == 8.f);
    CHECK(w.read(view, 0, 0.f) == 40.f);
    CHECK(w.read(view, 0, 0.25f) == 50.f);
    CHECK(w.read(view, 0, 0.75f) == 70.f);
    CHECK(w.read(view, 0, 0.875f) == doctest::Approx(55.f)); // 70 back to 40
    // An end of 0 is the end of the buffer.
    w.start(10.f).end(0.f);
    CHECK(w.read(view, 0, 0.f) == 100.f);
    CHECK(w.read(view, 0, 0.5f) == 150.f);
    // Past the end is the end.
    w.end(1000.f);
    CHECK(w.read(view, 0, 0.5f) == 150.f);
    // An end at or before the start, or a start past the buffer: silence.
    w.start(8.f).end(8.f);
    CHECK(w.read(view, 0, 0.5f) == 0.f);
    w.start(9.f).end(3.f);
    CHECK(w.read(view, 0, 0.5f) == 0.f);
    w.start(500.f).end(0.f);
    CHECK(w.read(view, 0, 0.5f) == 0.f);
    buffer phase = Constant(16, 0.5f);
    buffer& out = w(view, phase);
    for (UInt i = 0; i < 16; ++i)
      CHECK(out.getPtr()[i] == 0.f);
    // Below 0 is 0.
    w.start(-50.f).end(0.f);
    CHECK(w.read(view, 0, 0.5f) == 100.f);

    // At 2000 Hz a millisecond is two frames.
    b->setSampleRate(2000.f);
    sampleView faster = reader.pin();
    w.start(2.f).end(6.f); // frames 4 .. 11
    CHECK(w.read(faster, 0, 0.f) == 40.f);
    CHECK(w.read(faster, 0, 0.5f) == 80.f);
  }

  TEST_CASE("waveReader: every interpolation mode against its closed form (#1146)") {
    // Four frames 0, 1, 4, 9 around the position read: frame 1 + f.
    auto b = Values({0.f, 1.f, 4.f, 9.f, 16.f, 25.f, 36.f, 49.f});
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    waveReader w;
    const float y0 = 0.f, y1 = 1.f, y2 = 4.f, y3 = 9.f;
    for (float f : {0.f, 0.25f, 0.5f, 0.8f}) {
      CAPTURE(f);
      const float phase = (1.f + f) / 8.f;
      w.interp(interp::NONE);
      CHECK(w.read(view, 0, phase) == y1);
      w.interp(interp::LINEAR);
      CHECK(w.read(view, 0, phase) == doctest::Approx(y1 + (y2 - y1) * f));
      w.interp(interp::LINEAR_LOW);
      CHECK(w.read(view, 0, phase) == doctest::Approx(y1 + (y2 - y1) * f));
      w.interp(interp::COSINE);
      const double g = (1.0 - std::cos(f * 3.141592653589793)) / 2.0;
      CHECK(w.read(view, 0, phase) == doctest::Approx(y1 + (y2 - y1) * g));
      w.interp(interp::CUBIC);
      const float a0 = y3 - y2 - y0 + y1;
      const float a1 = y0 - y1 - a0;
      const float a2 = y2 - y0;
      CHECK(w.read(view, 0, phase) == doctest::Approx(a0 * f * f * f + a1 * f * f + a2 * f + y1));
      // Catmull-Rom follows a parabola exactly: frame n is n^2.
      w.interp(interp::SPLINE);
      CHECK(w.read(view, 0, phase) == doctest::Approx((1.f + f) * (1.f + f)));
      // Hermite at tension 0 and bias 0 is the spline.
      w.interp(interp::HERMITE).tension(0.f).bias(0.f);
      CHECK(w.read(view, 0, phase) == doctest::Approx((1.f + f) * (1.f + f)));
      // At tension 1 both slopes are 0: smoothstep between y1 and y2.
      w.tension(1.f);
      CHECK(w.read(view, 0, phase) ==
            doctest::Approx(y1 + (y2 - y1) * (3.f * f * f - 2.f * f * f * f)));
      // Bias 1 takes each slope from the segment before alone.
      w.tension(0.f).bias(1.f);
      const float m0 = y1 - y0;
      const float m1 = y2 - y1;
      const float f2 = f * f;
      const float f3 = f2 * f;
      CHECK(w.read(view, 0, phase) ==
            doctest::Approx((2 * f3 - 3 * f2 + 1) * y1 + (f3 - 2 * f2 + f) * m0 + (f3 - f2) * m1 +
                            (-2 * f3 + 3 * f2) * y2));
      w.bias(0.f);
    }
    CHECK(w.tension() == 0.f);
    CHECK(w.bias() == 0.f);
    // The neighbours wrap around the region: frame 0 + 0.5 sees frame 7
    // before it.
    w.interp(interp::SPLINE);
    const float p0 = 49.f, p1 = 0.f, p2 = 1.f, p3 = 4.f, f = 0.5f;
    const float c0 = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    const float c1 = p0 - 2.5f * p1 + 2.f * p2 - 0.5f * p3;
    const float c2 = -0.5f * p0 + 0.5f * p2;
    CHECK(w.read(view, 0, 0.5f / 8.f) ==
          doctest::Approx(c0 * f * f * f + c1 * f * f + c2 * f + p1));
  }

  TEST_CASE("waveReader: a sine table reads back as a sine (#1146)") {
    // One cycle of a sine in 64 frames, read at 1000 phases: each mode
    // within its error bound of sin(2 pi p).
    constexpr UInt N = 64;
    auto b = sampleBuffer::create(1, N, 1000.f);
    std::vector<float> v(N);
    for (UInt n = 0; n < N; ++n)
      v[n] = static_cast<float>(std::sin(TWO_PI * n / N));
    b->setSamples(0, 0, v.data(), N);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    waveReader w;
    const double h = TWO_PI / N;
    struct bound {
      interp mode;
      double error;
    };
    // Linear: h^2 / 8. The spline and Hermite: h^3 / 8. The cosine bends a
    // straight line between two samples by up to 0.106 of a step, the
    // 4-point cubic polynomial by f (2f - 1) (f - 1), up to 0.097 of one:
    // a step of a sine is at most h.
    for (const bound& m :
         {bound{interp::LINEAR, h * h / 8.0 + 1e-6}, bound{interp::COSINE, 0.106 * h + h * h / 8.0},
          bound{interp::CUBIC, 0.1 * h + h * h / 8.0}, bound{interp::SPLINE, h * h * h / 8.0},
          bound{interp::HERMITE, h * h * h / 8.0}}) {
      CAPTURE(static_cast<int>(m.mode));
      w.interp(m.mode);
      double worst = 0.0;
      for (int k = 0; k < 1000; ++k) {
        const double p = (k + 0.37) / 1000.0;
        const double error =
            std::fabs(w.read(view, 0, static_cast<float>(p)) - std::sin(TWO_PI * p));
        worst = std::max(worst, error);
      }
      CHECK(worst <= m.error);
    }
  }

  TEST_CASE("waveReader: rows cut the region, the row phase crossfades between two (#1146)") {
    auto b = Tens(8); // rows of 4 frames: 0 10 20 30 | 40 50 60 70
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    waveReader w;
    w.rows(2);
    CHECK(w.rows() == 2);
    CHECK(w.read(view, 0, 0.25f, 0.f) == 10.f);
    CHECK(w.read(view, 0, 0.25f, 1.f) == 50.f);
    CHECK(w.read(view, 0, 0.25f, 0.5f) == 30.f); // halfway between 10 and 50
    CHECK(w.read(view, 0, 0.25f, 0.25f) == 20.f);
    // Each row is its own cycle: its last frame leads back into its first.
    CHECK(w.read(view, 0, 0.875f, 1.f) == doctest::Approx(55.f)); // 70 back to 40
    // Below 0 is the first row, above 1 the last, not a number the first.
    CHECK(w.read(view, 0, 0.25f, -3.f) == 10.f);
    CHECK(w.read(view, 0, 0.25f, 7.f) == 50.f);
    CHECK(w.read(view, 0, 0.25f, std::numeric_limits<float>::quiet_NaN()) == 10.f);
    // The one-phase block reads the first row.
    buffer phase = Signal({0.f, 0.25f, 0.5f});
    CHECK(w(view, phase).getPtr()[1] == 10.f);
    // Row phase per sample.
    buffer rowPhase = Signal({0.f, 0.5f, 1.f});
    buffer& out = w(view, phase, rowPhase);
    REQUIRE(out.getLength() == 3);
    CHECK(out.getPtr()[0] == 0.f);
    CHECK(out.getPtr()[1] == 30.f);
    CHECK(out.getPtr()[2] == 60.f);

    // Three rows of 8 frames: rows of 2, frames 6 and 7 not read.
    w.rows(3);
    CHECK(w.read(view, 0, 0.5f, 0.f) == 10.f);
    CHECK(w.read(view, 0, 0.5f, 0.5f) == 30.f);
    CHECK(w.read(view, 0, 0.5f, 1.f) == 50.f);
    CHECK(w.read(view, 0, 0.5f, 0.75f) == 40.f); // halfway between rows 1 and 2
    // 0 and 1 are one row: the row phase is not used.
    for (UInt n : {0u, 1u}) {
      w.rows(n);
      CHECK(w.read(view, 0, 0.5f, 1.f) == 40.f);
    }
    // More rows than frames: no frame a row, silence.
    w.rows(9);
    CHECK(w.read(view, 0, 0.5f, 0.f) == 0.f);
  }

  TEST_CASE("waveReader: output k reads buffer channel k (#1146)") {
    auto b = Tens(4, 3);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    waveReader w(2);
    CHECK(w.channels() == 2);
    buffer phase = Signal({0.25f, 0.5f});
    w(view, phase);
    CHECK(w.out(0).getPtr()[0] == 10.f);
    CHECK(w.out(1).getPtr()[0] == 20.f);
    CHECK(w.out(1).getPtr()[1] == 40.f);
    CHECK(&w.out(7) == &w.out(1)); // past the count: the last
    CHECK(w.read(view, 2, 0.25f) == 30.f);
    CHECK(w.read(view, 3, 0.25f) == 0.f);

    // More outputs than channels: the rest are silent.
    w.channels(4);
    w(view, phase);
    CHECK(w.out(2).getPtr()[0] == 30.f);
    CHECK(w.out(3).getPtr()[0] == 0.f);
    CHECK(w.out(3).getLength() == 2);
    w.channels(0);
    CHECK(w.channels() == 1);
    w.channels(1000);
    CHECK(w.channels() == waveReader::MAX_CHANNELS);
  }

  TEST_CASE("waveReader: silence for a phase that is not finite, no buffer, an empty one "
            "(#1146)") {
    auto b = Tens(8);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    waveReader w;
    CHECK(w.read(view, 0, std::numeric_limits<float>::quiet_NaN()) == 0.f);
    CHECK(w.read(view, 0, std::numeric_limits<float>::infinity()) == 0.f);
    buffer phase = Signal({0.5f, std::numeric_limits<float>::quiet_NaN(),
                           -std::numeric_limits<float>::infinity(), 0.25f});
    buffer& out = w(view, phase);
    CHECK(out.getPtr()[0] == 40.f);
    CHECK(out.getPtr()[1] == 0.f);
    CHECK(out.getPtr()[2] == 0.f);
    CHECK(out.getPtr()[3] == 20.f);

    sampleBufferReader unbound;
    sampleView none = unbound.pin();
    CHECK(w.read(none, 0, 0.5f) == 0.f);
    buffer& silent = w(none, phase);
    for (UInt i = 0; i < 4; ++i)
      CHECK(silent.getPtr()[i] == 0.f);

    auto empty = sampleBuffer::create(1, 0, 1000.f);
    sampleBufferReader emptyReader(empty);
    sampleView nothing = emptyReader.pin();
    CHECK(w.read(nothing, 0, 0.5f) == 0.f);
  }

  TEST_CASE("waveReader: block lengths (#1146)") {
    auto b = Tens(8);
    sampleBufferReader reader(b);
    sampleView view = reader.pin();
    waveReader w;
    buffer longer = Constant(YSE::STANDARD_BUFFERSIZE * 2, 0.5f);
    CHECK(w(view, longer).getLength() == YSE::STANDARD_BUFFERSIZE);
    buffer shorter = Constant(17, 0.5f);
    CHECK(w(view, shorter).getLength() == 17);
    buffer rows = Constant(9, 0.f);
    CHECK(w(view, shorter, rows).getLength() == 9);
    buffer full = Constant(YSE::STANDARD_BUFFERSIZE, 0.25f);
    buffer& out = w(view, full);
    REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(out.getPtr()[YSE::STANDARD_BUFFERSIZE - 1] == 20.f);
  }

  TEST_CASE("waveReader: the region follows the buffer's rate, not a stored one (#1146)") {
    SampleRateGuard guard;
    // A buffer at its own rate: the engine's rate changes nothing.
    auto own = Tens(100, 1, 1000.f);
    sampleBufferReader ownReader(own);
    waveReader w;
    w.start(10.f).end(20.f);
    YSE::SAMPLERATE = 44100;
    CHECK(w.read(ownReader.pin(), 0, 0.5f) == 150.f);
    YSE::SAMPLERATE = 48000;
    CHECK(w.read(ownReader.pin(), 0, 0.5f) == 150.f);

    // A buffer at the engine's rate (0): the region follows the live rate.
    auto engine = Tens(2000, 1, 0.f);
    sampleBufferReader engineReader(engine);
    YSE::SAMPLERATE = 10000; // 10 frames a ms: frames 100 .. 199
    CHECK(w.read(engineReader.pin(), 0, 0.f) == 1000.f);
    YSE::SAMPLERATE = 20000; // frames 200 .. 399
    CHECK(w.read(engineReader.pin(), 0, 0.f) == 2000.f);
  }

  TEST_CASE("waveReader: blocks allocate nothing (#1146)") {
    auto b = Tens(1024, 2);
    sampleBufferReader reader(b);
    waveReader w(2);
    w.rows(4).start(1.f).end(900.f);
    buffer phase = Constant(YSE::STANDARD_BUFFERSIZE, 0.3f);
    buffer rowPhase = Constant(YSE::STANDARD_BUFFERSIZE, 0.6f);
    buffer shorter = Constant(64, 0.7f);
    w(reader.pin(), phase, rowPhase); // warm
    {
      TestHelpers::ProbeScope probe;
      for (interp mode : {interp::NONE, interp::LINEAR, interp::COSINE, interp::CUBIC,
                          interp::SPLINE, interp::HERMITE}) {
        w.interp(mode);
        sampleView view = reader.pin();
        w(view, phase, rowPhase);
        w(view, shorter);
        w(view, phase);
        (void)w.read(view, 1, 0.4f, 0.2f);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE
