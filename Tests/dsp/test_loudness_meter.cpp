// Tests for YSE::DSP::loudnessMeter (YseEngine/dsp/analysis/loudnessMeter.hpp),
// issue #1129. References: the K-weighting coefficients of ITU-R BS.1770-4
// at 48 kHz, the minimum-requirement test signals of EBU Tech 3341 (cases 1
// to 6: 1 kHz sines whose loudness is known in closed form), and a
// brute-force gating reference that keeps every block.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include "dsp/analysis/loudnessMeter.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::loudnessMeter;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;
  constexpr double Pi = 3.14159265358979323846;

  // A stretch of 1 kHz sines, one level in dBFS per channel (peak, as EBU
  // Tech 3341 gives them); a level below -500 is silence.
  struct Segment {
    double seconds;
    std::vector<double> dbfs;
  };

  // Runs the segments through `m` in blocks of `block` samples at the live
  // rate, the sine phase running on across segments.
  void Run(loudnessMeter& m, const std::vector<Segment>& segments, UInt block = N,
           double frequency = 1000.0) {
    const double rate = static_cast<double>(YSE::SAMPLERATE);
    const std::size_t channels = segments.front().dbfs.size();
    std::vector<buffer> bufs;
    bufs.reserve(channels);
    for (std::size_t c = 0; c < channels; ++c)
      bufs.emplace_back(block);
    std::vector<buffer*> ptrs;
    ptrs.reserve(channels);
    for (buffer& b : bufs)
      ptrs.push_back(&b);
    std::uint64_t n = 0;
    for (const Segment& s : segments) {
      const auto total = static_cast<std::uint64_t>(std::llround(s.seconds * rate));
      std::vector<double> amp;
      amp.reserve(s.dbfs.size());
      for (double db : s.dbfs)
        amp.push_back(db < -500.0 ? 0.0 : std::pow(10.0, db / 20.0));
      std::uint64_t done = 0;
      while (done < total) {
        const UInt len = static_cast<UInt>(std::min<std::uint64_t>(block, total - done));
        for (std::size_t c = 0; c < channels; ++c) {
          if (bufs[c].getLength() != len) bufs[c].resize(len);
          float* p = bufs[c].getPtr();
          for (UInt i = 0; i < len; ++i)
            p[i] = static_cast<float>(
                amp[c] * std::sin(2.0 * Pi * frequency * static_cast<double>(n + i) / rate));
        }
        m(ptrs.data(), static_cast<UInt>(channels));
        n += len;
        done += len;
      }
    }
  }

  std::vector<double> Stereo(double db) {
    return {db, db};
  }

  // |H(f)|^2 of one section at `rate`.
  double Power(const loudnessMeter::section& s, double f, double rate) {
    const double w = 2.0 * Pi * f / rate;
    const double cr = std::cos(w);
    const double ci = -std::sin(w);
    const double c2r = std::cos(2.0 * w);
    const double c2i = -std::sin(2.0 * w);
    const double nr = s.b0 + s.b1 * cr + s.b2 * c2r;
    const double ni = s.b1 * ci + s.b2 * c2i;
    const double dr = 1.0 + s.a1 * cr + s.a2 * c2r;
    const double di = s.a1 * ci + s.a2 * c2i;
    return (nr * nr + ni * ni) / (dr * dr + di * di);
  }

  double KWeightDb(double f, double rate) {
    return 10.0 * std::log10(Power(loudnessMeter::Shelf(rate), f, rate) *
                             Power(loudnessMeter::HighPass(rate), f, rate));
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("loudnessMeter: K-weighting matches BS.1770's 48 kHz coefficients (#1129)") {
    const loudnessMeter::section shelf = loudnessMeter::Shelf(48000.0);
    CHECK(shelf.b0 == doctest::Approx(1.53512485958697).epsilon(1e-9));
    CHECK(shelf.b1 == doctest::Approx(-2.69169618940638).epsilon(1e-9));
    CHECK(shelf.b2 == doctest::Approx(1.19839281085285).epsilon(1e-9));
    CHECK(shelf.a1 == doctest::Approx(-1.69065929318241).epsilon(1e-9));
    CHECK(shelf.a2 == doctest::Approx(0.73248077421585).epsilon(1e-9));
    const loudnessMeter::section hp = loudnessMeter::HighPass(48000.0);
    CHECK(hp.b0 == 1.0);
    CHECK(hp.b1 == -2.0);
    CHECK(hp.b2 == 1.0);
    CHECK(hp.a1 == doctest::Approx(-1.99004745483398).epsilon(1e-9));
    CHECK(hp.a2 == doctest::Approx(0.99007225036621).epsilon(1e-9));
  }

  TEST_CASE("loudnessMeter: the K-weighting keeps its response at other sample rates (#1129)") {
    // About +0.69 dB at 1 kHz, the -0.691 in the loudness formula undoes it.
    for (double rate : {22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
      CAPTURE(rate);
      // The bilinear transform bends the curve a little away from the
      // prototype's corner frequencies: well within Tech 3341's 0.1 LU.
      for (double f : {100.0, 997.0, 5000.0, 9000.0})
        CHECK(std::fabs(KWeightDb(f, rate) - KWeightDb(f, 48000.0)) <= 0.08);
      CHECK(KWeightDb(20.0, rate) < -10.0); // the high-pass
    }
    CHECK(KWeightDb(1000.0, 48000.0) == doctest::Approx(0.691).epsilon(0.01));
    CHECK(loudnessMeter::Shelf(0.0).b0 == 1.0); // no rate: passes
  }

  TEST_CASE("loudnessMeter: a 1 kHz sine at -20 dBFS on one channel is -23 LUFS (#1129)") {
    ScopedSampleRate rate(48000);
    loudnessMeter m;
    Run(m, {{4.0, {-20.0}}});
    CHECK(m.momentary() == doctest::Approx(-23.0).epsilon(0.1 / 23.0));
    CHECK(m.shortTerm() == doctest::Approx(-23.0).epsilon(0.1 / 23.0));
    CHECK(m.integrated() == doctest::Approx(-23.0).epsilon(0.1 / 23.0));
    CHECK(m.peak() == doctest::Approx(-20.0).epsilon(1e-3));
  }

  TEST_CASE("loudnessMeter: EBU Tech 3341 cases 1 and 2, stereo sines at -23 and -33 dBFS "
            "(#1129)") {
    for (UInt sr : {48000u, 44100u, 96000u}) {
      CAPTURE(sr);
      ScopedSampleRate rate(sr);
      for (double level : {-23.0, -33.0}) {
        CAPTURE(level);
        loudnessMeter m(2);
        Run(m, {{20.0, Stereo(level)}});
        CHECK(std::fabs(m.momentary() - level) <= 0.1);
        CHECK(std::fabs(m.shortTerm() - level) <= 0.1);
        CHECK(std::fabs(m.integrated() - level) <= 0.1);
      }
    }
  }

  TEST_CASE("loudnessMeter: EBU Tech 3341 cases 3 to 5, the relative gate (#1129)") {
    ScopedSampleRate rate(48000);
    {
      loudnessMeter m(2);
      Run(m, {{10.0, Stereo(-36.0)}, {60.0, Stereo(-23.0)}, {10.0, Stereo(-36.0)}});
      CHECK(std::fabs(m.integrated() - -23.0) <= 0.1);
    }
    {
      loudnessMeter m(2);
      Run(m, {{10.0, Stereo(-72.0)},
              {10.0, Stereo(-36.0)},
              {60.0, Stereo(-23.0)},
              {10.0, Stereo(-36.0)},
              {10.0, Stereo(-72.0)}});
      CHECK(std::fabs(m.integrated() - -23.0) <= 0.1);
    }
    {
      loudnessMeter m(2);
      Run(m, {{20.0, Stereo(-26.0)}, {20.1, Stereo(-20.0)}, {20.0, Stereo(-26.0)}});
      CHECK(std::fabs(m.integrated() - -23.0) <= 0.1);
    }
  }

  TEST_CASE("loudnessMeter: EBU Tech 3341 case 6, five channels with surround weights "
            "(#1129)") {
    ScopedSampleRate rate(48000);
    loudnessMeter m(5);
    m.weight(3, 1.41f).weight(4, 1.41f);
    CHECK(m.weight(3) == 1.41f);
    CHECK(m.weight(0) == 1.f);
    // L, R, C, Ls, Rs.
    Run(m, {{20.0, {-28.0, -28.0, -24.0, -30.0, -30.0}}});
    CHECK(std::fabs(m.integrated() - -23.0) <= 0.1);
    CHECK(m.peak() == doctest::Approx(-24.0).epsilon(1e-3)); // C
    CHECK(m.peak(1) == doctest::Approx(-28.0).epsilon(1e-3));
    CHECK(m.peak(5) == doctest::Approx(-30.0).epsilon(1e-3));
    CHECK(m.peak(6) == m.peak()); // out of range: all
  }

  TEST_CASE("loudnessMeter: momentary and short-term windows fill with silence first "
            "(#1129)") {
    ScopedSampleRate rate(48000);
    loudnessMeter m;
    CHECK(m.momentary() == loudnessMeter::SILENCE);
    CHECK(m.integrated() == loudnessMeter::SILENCE);
    // 200 ms of a steady sine: half the momentary window, -3 dB.
    Run(m, {{0.2, {-20.0}}});
    CHECK(m.momentary() == doctest::Approx(-26.0).epsilon(0.15 / 26.0));
    CHECK(m.gatedBlocks() == 0); // no 400 ms block yet
    CHECK(m.integrated() == loudnessMeter::SILENCE);
    // 1.5 s: the momentary window is full, the short-term half full.
    Run(m, {{1.3, {-20.0}}});
    CHECK(m.momentary() == doctest::Approx(-23.0).epsilon(0.1 / 23.0));
    CHECK(m.shortTerm() == doctest::Approx(-26.0).epsilon(0.15 / 26.0));
    CHECK(m.gatedBlocks() == 12); // a block every 100 ms from 400 ms on
    const double steady = m.integrated();
    CHECK(std::fabs(steady - -23.0) <= 0.1);
    // Then silence: momentary goes to silence after 400 ms, short-term
    // after 3 s; the integrated loudness stays.
    Run(m, {{0.5, {-1000.0}}});
    CHECK(m.momentary() < -150.f);
    CHECK(m.shortTerm() > -30.f);
    Run(m, {{3.0, {-1000.0}}});
    CHECK(m.shortTerm() < -150.f);
    // The fade-out blocks hold 3/4, 1/2 and 1/4 of the sine and pass the
    // relative gate: 13.5 block energies over 15 blocks, the silent ones
    // gated out. The filters' decay keeps the first silent block above
    // -70 LUFS, but far below the relative gate.
    CHECK(m.gatedBlocks() == 16);
    CHECK(m.integrated() == doctest::Approx(steady + 10.0 * std::log10(13.5 / 15.0)).epsilon(1e-4));
  }

  TEST_CASE("loudnessMeter: the absolute gate leaves out everything at -70 LUFS and below "
            "(#1129)") {
    ScopedSampleRate rate(48000);
    loudnessMeter m;
    Run(m, {{2.0, {-75.0}}});
    CHECK(m.momentary() == doctest::Approx(-78.0).epsilon(0.1 / 78.0));
    CHECK(m.gatedBlocks() == 0);
    CHECK(m.integrated() == loudnessMeter::SILENCE);
    CHECK(m.peak() == doctest::Approx(-75.0).epsilon(1e-3));
  }

  TEST_CASE("loudnessMeter: the histogram matches gating every block exactly (#1129)") {
    ScopedSampleRate rate(48000);
    // 100 ms steps at levels spread over 70 dB, some below the gates.
    std::vector<Segment> segments;
    std::uint32_t seed = 1129;
    for (int i = 0; i < 300; ++i) {
      seed = seed * 1664525u + 1013904223u;
      const double db = -75.0 + 70.0 * static_cast<double>(seed >> 8) / 16777216.0;
      segments.push_back({0.1, {db}});
    }
    loudnessMeter m;
    Run(m, segments, 37);

    // The reference: the same signal through the same coefficients, every
    // block kept.
    const double sr = 48000.0;
    const loudnessMeter::section a = loudnessMeter::Shelf(sr);
    const loudnessMeter::section b = loudnessMeter::HighPass(sr);
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, z1 = 0, z2 = 0;
    std::vector<double> steps;
    double acc = 0.0;
    std::uint64_t n = 0;
    for (const Segment& s : segments) {
      const double amp = std::pow(10.0, s.dbfs[0] / 20.0);
      for (int i = 0; i < 4800; ++i, ++n) {
        const double x = static_cast<double>(
            static_cast<float>(amp * std::sin(2.0 * Pi * 1000.0 * static_cast<double>(n) / sr)));
        const double y = a.b0 * x + a.b1 * x1 + a.b2 * x2 - a.a1 * y1 - a.a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        const double z = b.b0 * y + b.b1 * u1 + b.b2 * u2 - b.a1 * z1 - b.a2 * z2;
        u2 = u1;
        u1 = y;
        z2 = z1;
        z1 = z;
        acc += z * z;
      }
      steps.push_back(acc);
      acc = 0.0;
    }
    std::vector<double> blocks;
    for (std::size_t k = 3; k < steps.size(); ++k) {
      const double e = (steps[k] + steps[k - 1] + steps[k - 2] + steps[k - 3]) / (4.0 * 4800.0);
      if (-0.691 + 10.0 * std::log10(e) > -70.0) blocks.push_back(e);
    }
    REQUIRE(m.gatedBlocks() == blocks.size());
    double mean = 0.0;
    for (double e : blocks)
      mean += e;
    mean /= static_cast<double>(blocks.size());
    const double gate = -0.691 + 10.0 * std::log10(mean) - 10.0;
    double sum = 0.0;
    std::size_t count = 0;
    for (double e : blocks) {
      if (-0.691 + 10.0 * std::log10(e) > gate) {
        sum += e;
        count++;
      }
    }
    const double want = -0.691 + 10.0 * std::log10(sum / static_cast<double>(count));
    CHECK(m.integrated() == doctest::Approx(want).epsilon(0.005 / std::fabs(want)));
    CHECK(std::fabs(m.integrated() - want) <= 0.005);
  }

  TEST_CASE("loudnessMeter: block splits give the same measurement (#1129)") {
    ScopedSampleRate rate(44100);
    const std::vector<Segment> segments = {{1.0, Stereo(-30.0)}, {1.3, Stereo(-18.0)}};
    loudnessMeter whole(2);
    Run(whole, segments, N);
    for (UInt block : {64u, 13u, 1u}) {
      CAPTURE(block);
      loudnessMeter m(2);
      Run(m, segments, block);
      CHECK(m.momentary() == doctest::Approx(whole.momentary()).epsilon(1e-5));
      CHECK(m.shortTerm() == doctest::Approx(whole.shortTerm()).epsilon(1e-5));
      CHECK(m.integrated() == doctest::Approx(whole.integrated()).epsilon(1e-5));
      CHECK(m.gatedBlocks() == whole.gatedBlocks());
      CHECK(m.peak() == whole.peak());
    }
  }

  TEST_CASE("loudnessMeter: missing and short channels read as silence (#1129)") {
    ScopedSampleRate rate(48000);
    loudnessMeter two(2);
    loudnessMeter one(1);
    buffer full(N);
    for (UInt i = 0; i < N; ++i)
      full.getPtr()[i] = 0.25f * static_cast<float>(std::sin(0.1 * i));
    buffer* only[2] = {&full, nullptr};
    for (int k = 0; k < 400; ++k) {
      two(only, 2);
      one(full);
    }
    CHECK(two.momentary() == one.momentary());
    CHECK(two.integrated() == one.integrated());
    CHECK(two.peak(2) == loudnessMeter::SILENCE);

    // A shorter second channel: silence past its end, so its peak counts.
    buffer half(N / 2);
    half = 0.5f;
    buffer* pair[2] = {&full, &half};
    two(pair, 2);
    CHECK(two.peak(2) == doctest::Approx(20.0 * std::log10(0.5)).epsilon(1e-4));
    // More channels than the meter has are ignored; a mono call is channel 0.
    loudnessMeter mono(1);
    buffer* three[3] = {&full, &half, &half};
    mono(three, 3);
    CHECK(mono.peak() == doctest::Approx(one.peak()).epsilon(1e-6));
    CHECK(loudnessMeter(0).channels() == 1);
    CHECK(loudnessMeter(1000).channels() == loudnessMeter::MAX_CHANNELS);
  }

  TEST_CASE("loudnessMeter: NaN and infinities enter as 0 and do not poison the filters "
            "(#1129)") {
    ScopedSampleRate rate(48000);
    loudnessMeter clean;
    loudnessMeter dirty;
    Run(clean, {{1.0, {-20.0}}});
    Run(dirty, {{0.5, {-20.0}}});
    buffer bad(N);
    bad = 0.f;
    bad.getPtr()[3] = std::numeric_limits<float>::quiet_NaN();
    bad.getPtr()[9] = std::numeric_limits<float>::infinity();
    dirty(bad);
    Run(dirty, {{1.0, {-20.0}}});
    CHECK(std::isfinite(dirty.momentary()));
    CHECK(dirty.momentary() == doctest::Approx(clean.momentary()).epsilon(0.01));
    CHECK(dirty.peak() == doctest::Approx(-20.0).epsilon(1e-3));
  }

  TEST_CASE("loudnessMeter: interval reports keep to the live sample rate (#1129)") {
    ScopedSampleRate rate(48000);
    loudnessMeter m;
    buffer b(N);
    b = 0.f;
    UInt reports = 0;
    for (int k = 0; k < 375; ++k) // 1 s
      reports += m(b);
    CHECK(reports == 0); // off by default
    m.interval(100.f);
    CHECK(m.interval() == 100.f);
    reports = 0;
    for (int k = 0; k < 375; ++k)
      reports += m(b);
    CHECK(reports == 10);
    m.interval(-1.f);
    CHECK(m.interval() == 0.f);
    m.interval(std::numeric_limits<float>::quiet_NaN());
    CHECK(m.interval() == 0.f);
    m.interval(0.001f); // shorter than a sample: every sample
    CHECK(m(b) == N);
  }

  TEST_CASE("loudnessMeter: reset() and a new sample rate start over (#1129)") {
    ScopedSampleRate rate(48000);
    loudnessMeter m;
    m.weight(0, 2.f).interval(50.f);
    Run(m, {{1.0, {-20.0}}});
    CHECK(m.gatedBlocks() > 0);
    m.reset();
    CHECK(m.gatedBlocks() == 0);
    CHECK(m.momentary() == loudnessMeter::SILENCE);
    CHECK(m.integrated() == loudnessMeter::SILENCE);
    CHECK(m.peak() == loudnessMeter::SILENCE);
    CHECK(m.weight(0) == 2.f);
    CHECK(m.interval() == 50.f);
    m.weight(0, 1.f);
    Run(m, {{1.0, {-20.0}}});
    CHECK(m.gatedBlocks() > 0);
    {
      ScopedSampleRate other(44100);
      Run(m, {{0.05, {-20.0}}});
      CHECK(m.gatedBlocks() == 0); // restarted for the new rate
      Run(m, {{3.0, {-20.0}}});
      CHECK(std::fabs(m.integrated() - -23.0) <= 0.1);
    }
  }

  TEST_CASE("loudnessMeter: weights below 0 or not finite are ignored (#1129)") {
    loudnessMeter m(2);
    m.weight(1, -1.f);
    m.weight(1, std::numeric_limits<float>::infinity());
    m.weight(99, 3.f);
    CHECK(m.weight(1) == 1.f);
    CHECK(m.weight(99) == 0.f);
    m.weight(1, 0.f);
    CHECK(m.weight(1) == 0.f);
  }

  TEST_CASE("loudnessMeter: processing allocates nothing (#1129)") {
    ScopedSampleRate rate(48000);
    loudnessMeter m(2);
    buffer a(N);
    buffer b(N / 2);
    for (UInt i = 0; i < N; ++i)
      a.getPtr()[i] = 0.5f * static_cast<float>(std::sin(0.13 * i));
    b = 0.25f;
    buffer* pair[2] = {&a, &b};
    m.interval(10.f);
    {
      TestHelpers::ProbeScope probe;
      UInt reports = 0;
      for (int k = 0; k < 2000; ++k) {
        reports += m(pair, 2);
        if (k % 100 == 0) {
          (void)m.integrated();
          (void)m.peak(1);
        }
      }
      m(a);
      m.reset();
      m.interval(20.f);
      m(pair, 2);
      (void)m.integrated();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
      CHECK(reports > 0);
    }
  }

} // TEST_SUITE("dsp")
