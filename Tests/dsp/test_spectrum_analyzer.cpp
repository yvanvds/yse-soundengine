// Tests for YSE::DSP::spectrumAnalyzer (YseEngine/dsp/analysis/spectrumAnalyzer.hpp),
// issue #1170. Reference output is closed form or a direct DFT: frame k is
// taken after sample ceil(k * interval) of the stream; its bins are the
// direct, double-precision DFT of the latest size samples (0 before the
// first) under a periodic Hann window, scaled by 2 over the window's sum (1
// at DC and Nyquist). A sine of a bin's frequency reads its amplitude in that
// bin and half of it beside it; a cosine's phase at the frame's oldest
// sample reads in its bin.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>
#include <vector>
#include "dsp/analysis/spectrumAnalyzer.hpp"
#include "dsp/spectral/stft.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::spectrumAnalyzer;
using YSE::DSP::stftFrame;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  // Feeds samples `from` .. `from + count - 1` of f(n) in blocks of `block`.
  template <typename F>
  void Feed(spectrumAnalyzer& s, F f, unsigned int from, unsigned int count, unsigned int block) {
    unsigned int n = 0;
    while (n < count) {
      const unsigned int length = (count - n) < block ? (count - n) : block;
      buffer b(length);
      for (unsigned int i = 0; i < length; ++i)
        b.getPtr()[i] = f(from + n + i);
      s(b);
      n += length;
    }
  }

  // The amplitude (or phase) spectrum the analyser must show after samples
  // 0 .. end - 1 of f(n), size samples a frame.
  template <typename F>
  std::vector<double> Reference(F f, unsigned int end, unsigned int size, bool phase = false) {
    std::vector<double> x(size, 0.0);
    double sum = 0.0;
    for (unsigned int k = 0; k < size; ++k) {
      const long n = static_cast<long>(end) - static_cast<long>(size) + static_cast<long>(k);
      const double w = 0.5 - 0.5 * std::cos(2.0 * PI * k / size);
      sum += w;
      float v = n >= 0 ? f(static_cast<unsigned int>(n)) : 0.f;
      if (!std::isfinite(v)) v = 0.f;
      x[k] = static_cast<double>(v) * w;
    }
    std::vector<double> out(size / 2 + 1);
    for (unsigned int j = 0; j <= size / 2; ++j) {
      double re = 0.0;
      double im = 0.0;
      for (unsigned int k = 0; k < size; ++k) {
        re += x[k] * std::cos(2.0 * PI * j * k / size);
        im -= x[k] * std::sin(2.0 * PI * j * k / size);
      }
      if (phase) {
        out[j] = std::atan2(im, re);
      } else {
        const double edge = (j == 0 || j == size / 2) ? 1.0 : 2.0;
        out[j] = std::hypot(re, im) * edge / sum;
      }
    }
    return out;
  }

  void CheckAgainst(const spectrumAnalyzer::frame& f, const std::vector<double>& expected,
                    double tolerance) {
    REQUIRE(f.values != nullptr);
    REQUIRE(f.bins == expected.size());
    for (UInt j = 0; j < f.bins; ++j) {
      CAPTURE(j);
      CHECK(std::fabs(static_cast<double>(f.values[j]) - expected[j]) < tolerance);
    }
  }

  float Rich(unsigned int n) {
    unsigned int s = n * 2654435761u + 12345u;
    s ^= s >> 13;
    s *= 2246822519u;
    s ^= s >> 16;
    return 0.5f * static_cast<float>(std::sin(0.07 * static_cast<double>(n))) +
           0.3f * (static_cast<float>(s >> 8) / 16777216.f - 0.5f);
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("spectrumAnalyzer: defaults, capacity, and no frame before the first one finishes "
            "(#1170)") {
    ScopedSampleRate rate(44100);
    spectrumAnalyzer s;
    CHECK(s.capacity() == spectrumAnalyzer::MAX_SIZE);
    CHECK(s.size() == 4096);
    CHECK(s.interval() == 20.f);
    CHECK(s.mode() == spectrumAnalyzer::AMPLITUDE);
    CHECK(s.frames() == 0);
    spectrumAnalyzer::frame f = s.acquire();
    CHECK(f.serial == 0);
    CHECK(f.bins == 0);
    CHECK(f.values == nullptr);

    // 20 ms at 44.1 kHz is 882 samples: the first frame after sample 882.
    Feed(s, Rich, 0, 881, N);
    CHECK(s.frames() == 0);
    CHECK(s.acquire().serial == 0);
    Feed(s, Rich, 881, 1, N);
    CHECK(s.frames() == 1);
    f = s.acquire();
    CHECK(f.serial == 1);
    CHECK(f.size == 4096);
    CHECK(f.bins == 2049);
    CHECK(f.rate == 44100);
    CHECK(f.mode == spectrumAnalyzer::AMPLITUDE);

    // The capacity is a power of two, 16 to 16384; the size starts within it.
    spectrumAnalyzer small(1000);
    CHECK(small.capacity() == 1024);
    CHECK(small.size() == 1024);
    spectrumAnalyzer tiny(0);
    CHECK(tiny.capacity() == 16);
    CHECK(tiny.size() == 16);
    spectrumAnalyzer huge(1u << 20);
    CHECK(huge.capacity() == 16384);
  }

  TEST_CASE("spectrumAnalyzer: frame k comes after sample ceil(k * interval), across block "
            "splits (#1170)") {
    ScopedSampleRate rate(1000);
    // One sample at a time: the samples each frame comes after.
    spectrumAnalyzer s(64);
    s.size(16).interval(10.5f); // 10.5 samples
    std::vector<unsigned int> at;
    for (unsigned int n = 0; n < 200; ++n) {
      const UInt before = s.frames();
      Feed(s, Rich, n, 1, 1);
      if (s.frames() != before) at.push_back(n + 1);
    }
    REQUIRE(at.size() == 19);
    for (unsigned int k = 1; k <= at.size(); ++k)
      CHECK(at[k - 1] == static_cast<unsigned int>(std::ceil(k * 10.5)));

    // Whole blocks: the same frames, the last one taken after sample 200.
    for (unsigned int block : {7u, 13u, 64u, N}) {
      CAPTURE(block);
      spectrumAnalyzer b(64);
      b.size(16).interval(10.5f);
      Feed(b, Rich, 0, 200, block);
      CHECK(b.frames() == 19);
      const spectrumAnalyzer::frame f = b.acquire();
      CHECK(f.serial == 19);
      CheckAgainst(f, Reference(Rich, 200, 16), 1e-5);
    }
  }

  TEST_CASE("spectrumAnalyzer: the interval follows the live sample rate when each wait "
            "begins (#1170)") {
    for (UInt r : {8000u, 22050u, 44100u, 48000u, 96000u}) {
      CAPTURE(r);
      ScopedSampleRate rate(r);
      spectrumAnalyzer s(64);
      s.size(16).interval(25.f);
      const unsigned int wait = static_cast<unsigned int>(std::ceil(25.0 * r / 1000.0));
      Feed(s, Rich, 0, wait - 1, N);
      CHECK(s.frames() == 0);
      Feed(s, Rich, wait - 1, 1, N);
      CHECK(s.frames() == 1);
    }

    // A rate change applies from the next wait: 10 samples, then 20.
    ScopedSampleRate rate(1000);
    spectrumAnalyzer s(64);
    s.size(16).interval(10.f);
    Feed(s, Rich, 0, 10, 1);
    CHECK(s.frames() == 1); // the next wait, 10 samples, began at 1000 Hz
    Feed(s, Rich, 10, 10, 1);
    CHECK(s.frames() == 2);
    YSE::SAMPLERATE = 2000; // the wait after frame 2 began at 1000 Hz too
    Feed(s, Rich, 20, 10, 1);
    CHECK(s.frames() == 3);
    Feed(s, Rich, 30, 19, 1);
    CHECK(s.frames() == 3);
    Feed(s, Rich, 49, 1, 1);
    CHECK(s.frames() == 4);
    CHECK(s.acquire().rate == 2000);
  }

  TEST_CASE("spectrumAnalyzer: a sine reads its amplitude in its bin, a constant at DC, the "
            "Nyquist tone at the last bin (#1170)") {
    ScopedSampleRate rate(1000);
    static constexpr unsigned int SIZE = 256;
    const auto make = [] {
      auto s = std::make_unique<spectrumAnalyzer>(SIZE);
      s->interval(256.f); // one frame after the first 256 samples
      return s;
    };
    {
      auto s = make();
      const auto sine = [](unsigned int n) {
        return static_cast<float>(0.7 * std::sin(2.0 * PI * 16.0 * n / SIZE + 0.3));
      };
      Feed(*s, sine, 0, SIZE, N);
      const spectrumAnalyzer::frame f = s->acquire();
      REQUIRE(f.serial == 1);
      REQUIRE(f.bins == SIZE / 2 + 1);
      CHECK(f.size == SIZE);
      CHECK(f.values[16] == doctest::Approx(0.7f).epsilon(1e-4));
      CHECK(f.values[15] == doctest::Approx(0.35f).epsilon(1e-4));
      CHECK(f.values[17] == doctest::Approx(0.35f).epsilon(1e-4));
      for (UInt j = 0; j < f.bins; ++j)
        if (j < 15 || j > 17) CHECK(std::fabs(f.values[j]) < 1e-4f);
      CheckAgainst(f, Reference(sine, SIZE, SIZE), 1e-5);
    }
    {
      auto s = make();
      Feed(*s, [](unsigned int) { return 0.25f; }, 0, SIZE, N);
      const spectrumAnalyzer::frame f = s->acquire();
      CHECK(f.values[0] == doctest::Approx(0.25f).epsilon(1e-5));
      // The window's lobe: c * size / 4 in bin 1, scaled as a sine's.
      CHECK(f.values[1] == doctest::Approx(0.25f).epsilon(1e-5));
      CHECK(std::fabs(f.values[2]) < 1e-5f);
    }
    {
      auto s = make();
      Feed(*s, [](unsigned int n) { return (n & 1u) != 0 ? -0.3f : 0.3f; }, 0, SIZE, N);
      const spectrumAnalyzer::frame f = s->acquire();
      CHECK(f.values[SIZE / 2] == doctest::Approx(0.3f).epsilon(1e-5));
      CHECK(f.values[SIZE / 2 - 1] == doctest::Approx(0.3f).epsilon(1e-5));
    }
  }

  TEST_CASE("spectrumAnalyzer: the phase spectrum reads a cosine's phase at the frame's oldest "
            "sample (#1170)") {
    ScopedSampleRate rate(1000);
    static constexpr unsigned int SIZE = 128;
    spectrumAnalyzer s(SIZE);
    s.interval(200.f).mode(spectrumAnalyzer::PHASE);
    CHECK(s.mode() == spectrumAnalyzer::PHASE);
    // 200 samples: the frame covers samples 72 .. 199.
    const auto cosine = [](unsigned int n) {
      return static_cast<float>(0.5 * std::cos(2.0 * PI * 8.0 * (n - 72.0) / SIZE - 0.9));
    };
    Feed(s, cosine, 0, 200, 37);
    const spectrumAnalyzer::frame f = s.acquire();
    REQUIRE(f.serial == 1);
    CHECK(f.mode == spectrumAnalyzer::PHASE);
    CHECK(f.values[8] == doctest::Approx(-0.9f).epsilon(1e-4));
    const std::vector<double> expected = Reference(cosine, 200, SIZE, true);
    CHECK(f.values[8] == doctest::Approx(static_cast<float>(expected[8])).epsilon(1e-4));

    // Back to amplitudes from the next frame.
    s.mode(spectrumAnalyzer::AMPLITUDE);
    Feed(s, cosine, 200, 200, 37);
    const spectrumAnalyzer::frame a = s.acquire();
    CHECK(a.mode == spectrumAnalyzer::AMPLITUDE);
    CHECK(a.values[8] == doctest::Approx(0.5f).epsilon(1e-4));
  }

  TEST_CASE("spectrumAnalyzer: every frame is the latest size samples, the stream reading as 0 "
            "before its first (#1170)") {
    ScopedSampleRate rate(1000);
    for (unsigned int size : {16u, 64u, 512u}) {
      CAPTURE(size);
      spectrumAnalyzer s(512);
      s.size(size).interval(10.f);
      for (unsigned int k = 1; k <= 80; ++k) {
        Feed(s, Rich, (k - 1) * 10, 10, 3);
        const spectrumAnalyzer::frame f = s.acquire();
        REQUIRE(f.serial == k);
        if (k % 7 == 1) CheckAgainst(f, Reference(Rich, k * 10, size), 2e-5);
      }
    }
  }

  TEST_CASE("spectrumAnalyzer: a size change applies from the next frame over the samples "
            "taken in (#1170)") {
    ScopedSampleRate rate(1000);
    spectrumAnalyzer s(1024);
    s.size(256).interval(100.f);
    Feed(s, Rich, 0, 1000, N);
    CHECK(s.acquire().size == 256);
    s.size(1000); // to 1024
    CHECK(s.size() == 1024);
    Feed(s, Rich, 1000, 100, N);
    const spectrumAnalyzer::frame f = s.acquire();
    CHECK(f.size == 1024);
    CheckAgainst(f, Reference(Rich, 1100, 1024), 2e-5);
    // Held to the capacity, and to MIN_SIZE.
    s.size(5000);
    CHECK(s.size() == 1024);
    s.size(3);
    CHECK(s.size() == 16);
    s.size(17);
    CHECK(s.size() == 32);
  }

  TEST_CASE("spectrumAnalyzer: settings are held, NaN is ignored, non-finite samples count as 0 "
            "(#1170)") {
    ScopedSampleRate rate(1000);
    spectrumAnalyzer s(64);
    s.interval(1.f);
    CHECK(s.interval() == spectrumAnalyzer::MIN_INTERVAL);
    s.interval(1e6f);
    CHECK(s.interval() == spectrumAnalyzer::MAX_INTERVAL);
    s.interval(std::nanf(""));
    CHECK(s.interval() == spectrumAnalyzer::MAX_INTERVAL);
    s.interval(-5.f);
    CHECK(s.interval() == spectrumAnalyzer::MIN_INTERVAL);

    CHECK(spectrumAnalyzer::Size(std::nanf("")) == spectrumAnalyzer::DEFAULT_SIZE);
    CHECK(spectrumAnalyzer::Size(-3.f) == 16);
    CHECK(spectrumAnalyzer::Size(100.7f) == 128);
    CHECK(spectrumAnalyzer::Size(1024.f) == 1024);
    CHECK(spectrumAnalyzer::Size(1e9f) == 16384);
    CHECK(spectrumAnalyzer::Mode(1.f) == spectrumAnalyzer::PHASE);
    CHECK(spectrumAnalyzer::Mode(1.9f) == spectrumAnalyzer::PHASE);
    CHECK(spectrumAnalyzer::Mode(0.f) == spectrumAnalyzer::AMPLITUDE);
    CHECK(spectrumAnalyzer::Mode(2.f) == spectrumAnalyzer::AMPLITUDE);
    CHECK(spectrumAnalyzer::Mode(std::nanf("")) == spectrumAnalyzer::AMPLITUDE);

    const auto spiky = [](unsigned int n) {
      if (n % 5 == 0) return std::numeric_limits<float>::quiet_NaN();
      if (n % 7 == 0) return std::numeric_limits<float>::infinity();
      return Rich(n);
    };
    s.size(64).interval(10.f);
    Feed(s, spiky, 0, 100, N);
    const spectrumAnalyzer::frame f = s.acquire();
    REQUIRE(f.serial == 10);
    CheckAgainst(f, Reference(spiky, 100, 64), 1e-5);
  }

  TEST_CASE("spectrumAnalyzer: reset forgets the samples and starts the interval again (#1170)") {
    ScopedSampleRate rate(1000);
    spectrumAnalyzer s(64);
    s.size(64).interval(10.f);
    Feed(s, Rich, 0, 15, N);
    CHECK(s.frames() == 1);
    s.reset();
    CHECK(s.acquire().serial == 1); // published frames stay
    // Restarted at sample 1000 of Rich: the frame after ten more, the samples
    // before them zero.
    const auto later = [](unsigned int n) { return Rich(n + 1000); };
    Feed(s, later, 0, 9, N);
    CHECK(s.frames() == 1);
    Feed(s, later, 9, 1, N);
    CHECK(s.frames() == 2);
    CheckAgainst(s.acquire(), Reference(later, 10, 64), 1e-5);
  }

  TEST_CASE("spectrumAnalyzer: spectral frames are captured every interval in hops and "
            "published at their last bin (#1170)") {
    ScopedSampleRate rate(1000);
    static constexpr UInt SIZE = 64;
    static constexpr UInt BINS = SIZE / 2;
    static constexpr UInt HOP = 16;
    spectrumAnalyzer s(SIZE);
    s.interval(32.f); // two hops
    std::vector<Flt> re(BINS);
    std::vector<Flt> im(BINS);
    const auto frameOf = [&](unsigned int index) {
      for (UInt b = 0; b < BINS; ++b) {
        re[b] = static_cast<Flt>(index) + 0.25f * static_cast<Flt>(b);
        im[b] = 0.5f * static_cast<Flt>(b) - 1.f;
      }
    };
    // Four passes of eight bins a frame.
    const auto pass = [&](unsigned int index, UInt first) {
      stftFrame f;
      f.real = re.data() + first;
      f.imaginary = im.data() + first;
      f.firstBin = first;
      f.count = 8;
      f.bins = BINS;
      f.size = SIZE;
      f.hop = HOP;
      f.index = index;
      s(f);
    };
    for (unsigned int index = 0; index < 6; ++index) {
      CAPTURE(index);
      frameOf(index);
      for (UInt first = 0; first < BINS; first += 8) {
        pass(index, first);
        // Frames 1, 3 and 5 (the second of every two hops), at the last pass.
        CHECK(s.frames() == (first < 24 ? index / 2 : (index + 1) / 2));
      }
    }
    const spectrumAnalyzer::frame f = s.acquire();
    REQUIRE(f.serial == 3);
    CHECK(f.size == SIZE);
    CHECK(f.bins == BINS);
    frameOf(5);
    for (UInt b = 0; b < BINS; ++b) {
      CAPTURE(b);
      const double edge = b == 0 ? 1.0 : 2.0;
      CHECK(f.values[b] == doctest::Approx(std::hypot(re[b], im[b]) * edge / SIZE).epsilon(1e-6));
    }

    // Phases; and a frame larger than the capacity keeps the bins it holds.
    spectrumAnalyzer p(16);
    p.interval(10.f).mode(spectrumAnalyzer::PHASE);
    frameOf(2);
    stftFrame whole;
    whole.real = re.data();
    whole.imaginary = im.data();
    whole.firstBin = 0;
    whole.count = BINS;
    whole.bins = BINS;
    whole.size = SIZE;
    whole.hop = HOP;
    p(whole);
    const spectrumAnalyzer::frame q = p.acquire();
    REQUIRE(q.serial == 1);
    CHECK(q.bins == 16);
    CHECK(q.mode == spectrumAnalyzer::PHASE);
    for (UInt b = 0; b < 16; ++b)
      CHECK(q.values[b] == doctest::Approx(std::atan2(im[b], re[b])).epsilon(1e-6));
  }

  TEST_CASE("spectrumAnalyzer: the held frame stays put while newer ones finish; acquire takes "
            "the newest (#1170)") {
    ScopedSampleRate rate(1000);
    spectrumAnalyzer s(16);
    s.size(16).interval(10.f);
    Feed(s, [](unsigned int) { return 1.f; }, 0, 10, N);
    const spectrumAnalyzer::frame first = s.acquire();
    REQUIRE(first.serial == 1);
    const float dc = first.values[0];
    CHECK(dc > 0.f);
    // Three more frames of silence: the held one does not change.
    Feed(s, [](unsigned int) { return 0.f; }, 10, 40, N);
    CHECK(s.frames() == 5);
    CHECK(s.held().serial == 1);
    CHECK(first.values[0] == dc);
    const spectrumAnalyzer::frame newest = s.acquire();
    CHECK(newest.serial == 5); // 2 to 4 skipped
    CHECK(newest.values[0] == 0.f);
    CHECK(s.acquire().serial == 5); // nothing newer: keeps it
  }

  TEST_CASE("spectrumAnalyzer: a reader on another thread only ever sees whole frames (#1170)") {
    ScopedSampleRate rate(1000);
    static constexpr UInt SIZE = 64;
    static constexpr UInt BINS = SIZE / 2;
    spectrumAnalyzer s(SIZE);
    s.interval(10.f);
    std::atomic<bool> done{false};
    std::atomic<unsigned int> torn{0};
    std::atomic<unsigned int> reads{0};
    std::thread reader([&] {
      while (!done.load(std::memory_order_acquire)) {
        const spectrumAnalyzer::frame f = s.acquire();
        if (f.serial == 0) continue;
        // Every bin but DC holds 2 * serial / SIZE.
        const float want = 2.f * static_cast<float>(f.serial) / static_cast<float>(SIZE);
        for (UInt b = 1; b < f.bins; ++b)
          if (f.values[b] != want) torn.fetch_add(1);
        reads.fetch_add(1);
      }
    });
    std::vector<Flt> re(BINS);
    std::vector<Flt> im(BINS, 0.f);
    for (unsigned int k = 1; k <= 20000; ++k) {
      for (UInt b = 0; b < BINS; ++b)
        re[b] = static_cast<Flt>(k);
      for (UInt first = 0; first < BINS; first += 8) {
        stftFrame f;
        f.real = re.data() + first;
        f.imaginary = im.data() + first;
        f.firstBin = first;
        f.count = 8;
        f.bins = BINS;
        f.size = SIZE;
        f.hop = 16;
        s(f);
      }
    }
    done.store(true, std::memory_order_release);
    reader.join();
    CHECK(torn.load() == 0);
    CHECK(reads.load() > 0);
    CHECK(s.frames() == 20000);
  }

  TEST_CASE("spectrumAnalyzer: analysing, the setters and reading allocate nothing (#1170)") {
    ScopedSampleRate rate(1000);
    spectrumAnalyzer s;
    buffer in(N);
    for (UInt i = 0; i < N; ++i)
      in.getPtr()[i] = Rich(i);
    std::vector<Flt> re(64, 0.5f);
    std::vector<Flt> im(64, -0.5f);
    s(in);
    {
      TestHelpers::ProbeScope probe;
      s.size(1024).interval(10.f).mode(spectrumAnalyzer::PHASE);
      for (int k = 0; k < 8; ++k)
        s(in);
      s.size(16384).mode(spectrumAnalyzer::AMPLITUDE);
      for (int k = 0; k < 40; ++k)
        s(in);
      stftFrame f;
      f.real = re.data();
      f.imaginary = im.data();
      f.count = 64;
      f.bins = 64;
      f.size = 128;
      f.hop = 32;
      for (int k = 0; k < 4; ++k)
        s(f);
      s.reset();
      const spectrumAnalyzer::frame got = s.acquire();
      (void)s.held();
      (void)s.frames();
      CHECK(got.serial > 0);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
