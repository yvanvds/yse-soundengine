// Tests for YSE::DSP::loudnessMeter (YseEngine/dsp/analysis/loudnessMeter.hpp),
// issue #1129. References: the K-weighting coefficients of ITU-R BS.1770-4
// at 48 kHz, the minimum-requirement test signals of EBU Tech 3341 (cases 1
// to 6: 1 kHz sines whose loudness is known in closed form), and a
// brute-force gating reference that keeps every block. Issue #1237 adds the
// loudness range (EBU Tech 3342 cases 1 to 4 and its reference code) and
// the true peak (Tech 3341 cases 15 to 23). Issue #1240 adds Tech 3341
// cases 9 to 12, builds cases 20 to 23 as Table 1 defines them, and
// measures the test set's files (Tech 3341 cases 7 and 8, Tech 3342 cases 5
// and 6) when YSE_EBU_LOUDNESS_DIR points at them. Issue #1242 slides the
// momentary window in 5 ms sub-steps and adds the maxima, for Tech 3341
// cases 13 and 14.

#include <doctest/doctest.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "dsp/analysis/loudnessMeter.hpp"
#include "dsp/fileBuffer.hpp"
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
  // rate, the sine phase running on across segments. `after`, when given,
  // is called after every block with the seconds measured so far; with
  // blocks no longer than a 100 ms step it sees every momentary and
  // short-term value.
  void Run(loudnessMeter& m, const std::vector<Segment>& segments, UInt block = N,
           double frequency = 1000.0, const std::function<void(double)>& after = {}) {
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
        if (after) after(static_cast<double>(n) / rate);
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

  // ---- EBU Tech 3341 cases 9 to 12 (issue #1240): the short-term and
  // momentary windows, generated exactly as Table 1 gives them. Each is a
  // stereo 1 kHz sine like case 1, read after every 128-sample block so the
  // checks see every value the meter shows.

  TEST_CASE("loudnessMeter: EBU Tech 3341 case 9, short-term constant after 3 s (#1240)") {
    ScopedSampleRate rate(48000);
    // (1.34 s at -20 dBFS; 1.66 s at -30 dBFS) repeated 5 times: every 3 s
    // window holds one period.
    std::vector<Segment> segments;
    for (int k = 0; k < 5; ++k) {
      segments.push_back({1.34, Stereo(-20.0)});
      segments.push_back({1.66, Stereo(-30.0)});
    }
    loudnessMeter m(2);
    double low = 0.0;
    double high = -1000.0;
    int seen = 0;
    Run(m, segments, N, 1000.0, [&](double t) {
      if (t < 3.0) return;
      low = std::min(low, static_cast<double>(m.shortTerm()));
      high = std::max(high, static_cast<double>(m.shortTerm()));
      seen++;
    });
    REQUIRE(seen > 0);
    CHECK(low >= -23.1);
    CHECK(high <= -22.9);
  }

  TEST_CASE("loudnessMeter: EBU Tech 3341 case 10, the file-based maximum short-term "
            "loudness (#1240)") {
    ScopedSampleRate rate(48000);
    // 20 files: i * 0.15 s of silence, 3 s at -23 dBFS, 1 s of silence; the
    // meter starts over for each.
    for (int i = 0; i < 20; ++i) {
      CAPTURE(i);
      loudnessMeter m(2);
      double top = -1000.0;
      Run(m, {{i * 0.15, Stereo(-1000.0)}, {3.0, Stereo(-23.0)}, {1.0, Stereo(-1000.0)}}, N, 1000.0,
          [&](double) { top = std::max(top, static_cast<double>(m.shortTerm())); });
      CHECK(std::fabs(top - -23.0) <= 0.1);
    }
  }

  TEST_CASE("loudnessMeter: EBU Tech 3341 case 11, the live maximum short-term loudness "
            "(#1240)") {
    ScopedSampleRate rate(48000);
    // One signal of 20 tones: i * 0.15 s of silence, 3 s at -38 + i dBFS,
    // 3 - i * 0.15 s of silence; 6 s each. The maximum within each 6 s is
    // the tone's own: the one before is 1 dB quieter.
    std::vector<Segment> segments;
    for (int i = 0; i < 20; ++i) {
      segments.push_back({i * 0.15, Stereo(-1000.0)});
      segments.push_back({3.0, Stereo(-38.0 + i)});
      segments.push_back({3.0 - i * 0.15, Stereo(-1000.0)});
    }
    std::vector<double> top(20, -1000.0);
    std::vector<double> held(20, -1000.0);
    loudnessMeter m(2);
    Run(m, segments, N, 1000.0, [&](double t) {
      const auto i = static_cast<std::size_t>(std::min(19.0, std::floor((t - 1e-9) / 6.0)));
      top[i] = std::max(top[i], static_cast<double>(m.shortTerm()));
      held[i] = m.maxShortTerm(); // the meter's own maximum at the end
    });
    for (int i = 0; i < 20; ++i) {
      CAPTURE(i);
      CHECK(std::fabs(top[static_cast<std::size_t>(i)] - (-38.0 + i)) <= 0.1);
      CHECK(std::fabs(held[static_cast<std::size_t>(i)] - (-38.0 + i)) <= 0.1);
    }
  }

  TEST_CASE("loudnessMeter: EBU Tech 3341 case 12, momentary constant after 1 s (#1240)") {
    ScopedSampleRate rate(48000);
    // (0.18 s at -20 dBFS; 0.22 s at -30 dBFS) repeated 25 times: every
    // 400 ms window holds one period.
    std::vector<Segment> segments;
    for (int k = 0; k < 25; ++k) {
      segments.push_back({0.18, Stereo(-20.0)});
      segments.push_back({0.22, Stereo(-30.0)});
    }
    loudnessMeter m(2);
    double low = 0.0;
    double high = -1000.0;
    int seen = 0;
    Run(m, segments, N, 1000.0, [&](double t) {
      if (t < 1.0) return;
      low = std::min(low, static_cast<double>(m.momentary()));
      high = std::max(high, static_cast<double>(m.momentary()));
      seen++;
    });
    REQUIRE(seen > 0);
    CHECK(low >= -23.1);
    CHECK(high <= -22.9);
  }

  TEST_CASE("loudnessMeter: EBU Tech 3341 case 13, the file-based maximum momentary "
            "loudness (#1242)") {
    for (UInt sr : {48000u, 44100u}) {
      CAPTURE(sr);
      ScopedSampleRate rate(sr);
      // 20 files: i * 20 ms of silence, 400 ms at -23 dBFS, 1 s of
      // silence; the meter starts over for each. A window that moved in
      // 100 ms steps read up to 0.46 LU low here.
      for (int i = 0; i < 20; ++i) {
        CAPTURE(i);
        loudnessMeter m(2);
        double top = -1000.0;
        Run(m, {{i * 0.02, Stereo(-1000.0)}, {0.4, Stereo(-23.0)}, {1.0, Stereo(-1000.0)}}, N,
            1000.0, [&](double) { top = std::max(top, static_cast<double>(m.momentary())); });
        CHECK(std::fabs(top - -23.0) <= 0.1);
        CHECK(m.maxMomentary() == doctest::Approx(top).epsilon(1e-6));
      }
    }
  }

  TEST_CASE("loudnessMeter: EBU Tech 3341 case 14, the live maximum momentary loudness "
            "(#1242)") {
    ScopedSampleRate rate(48000);
    // One signal of 20 tones: i * 20 ms of silence, 400 ms at -38 + i dBFS,
    // 400 - i * 20 ms of silence; 800 ms each. Each tone is 1 dB above the
    // last, so the maximum read at the end of each 800 ms is that tone's.
    std::vector<Segment> segments;
    for (int i = 0; i < 20; ++i) {
      segments.push_back({i * 0.02, Stereo(-1000.0)});
      segments.push_back({0.4, Stereo(-38.0 + i)});
      segments.push_back({0.4 - i * 0.02, Stereo(-1000.0)});
    }
    std::vector<double> held(20, -1000.0);
    loudnessMeter m(2);
    Run(m, segments, N, 1000.0, [&](double t) {
      const auto i = static_cast<std::size_t>(std::min(19.0, std::floor((t - 1e-9) / 0.8)));
      held[i] = m.maxMomentary();
    });
    for (int i = 0; i < 20; ++i) {
      CAPTURE(i);
      CHECK(std::fabs(held[static_cast<std::size_t>(i)] - (-38.0 + i)) <= 0.1);
    }
  }

  TEST_CASE("loudnessMeter: the maxima hold the highest momentary and short-term loudness "
            "until reset() (#1242)") {
    ScopedSampleRate rate(48000);
    loudnessMeter m;
    CHECK(m.maxMomentary() == loudnessMeter::SILENCE);
    CHECK(m.maxShortTerm() == loudnessMeter::SILENCE);
    Run(m, {{4.0, {-20.0}}, {4.0, {-40.0}}});
    CHECK(m.momentary() < -40.f);
    CHECK(std::fabs(m.maxMomentary() - -23.0) <= 0.1);
    CHECK(std::fabs(m.maxShortTerm() - -23.0) <= 0.1);
    // The momentary loudness moves every 5 ms, not every 100 ms.
    float last = m.momentary();
    int changes = 0;
    Run(m, {{0.1, {-10.0}}}, 48, 1000.0, [&](double) {
      if (m.momentary() != last) changes++;
      last = m.momentary();
    });
    CHECK(changes >= 19);
    m.reset();
    CHECK(m.maxMomentary() == loudnessMeter::SILENCE);
    CHECK(m.maxShortTerm() == loudnessMeter::SILENCE);
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

  // ---- Loudness range (EBU Tech 3342) and true peak (BS.1770 Annex 2),
  // issue #1237.

  TEST_CASE("loudnessMeter: EBU Tech 3342 cases 1 to 4, the loudness range (#1237)") {
    struct Case {
      std::vector<double> levels;
      double want;
    };
    const std::vector<Case> cases = {
        {{-20.0, -30.0}, 10.0},
        {{-20.0, -15.0}, 5.0},
        {{-40.0, -20.0}, 20.0},
        {{-50.0, -35.0, -20.0, -35.0, -50.0}, 15.0},
    };
    for (UInt sr : {48000u, 44100u}) {
      CAPTURE(sr);
      ScopedSampleRate rate(sr);
      for (const Case& c : cases) {
        CAPTURE(c.want);
        std::vector<Segment> segments;
        segments.reserve(c.levels.size());
        for (double db : c.levels)
          segments.push_back({20.0, Stereo(db)});
        loudnessMeter m(2);
        Run(m, segments);
        CHECK(std::fabs(m.loudnessRange() - c.want) <= 1.0);
        // The steady tones fix both percentiles closely.
        CHECK(std::fabs(m.loudnessRange() - c.want) <= 0.1);
      }
    }
  }

  TEST_CASE("loudnessMeter: the loudness range matches Tech 3342's reference code on every "
            "short-term value (#1237)") {
    ScopedSampleRate rate(48000);
    loudnessMeter m;
    std::vector<double> shortTerm; // as Tech 3342's code takes them
    std::uint32_t seed = 1237;
    std::uint64_t n = 0;
    // 60 s in 100 ms steps over levels from -85 to -5 dBFS.
    for (int step = 0; step < 600; ++step) {
      seed = seed * 1664525u + 1013904223u;
      const double db = -85.0 + 80.0 * static_cast<double>(seed >> 8) / 16777216.0;
      const double amp = std::pow(10.0, db / 20.0);
      for (UInt done = 0; done < 4800; done += N) {
        buffer part(N);
        for (UInt i = 0; i < N; ++i, ++n)
          part.getPtr()[i] = static_cast<float>(
              amp * std::sin(2.0 * Pi * 997.0 * static_cast<double>(n) / 48000.0));
        m(part);
      }
      if (step >= 29) shortTerm.push_back(m.shortTerm());
    }
    // Tech 3342, Appendix: gate at -70 LUFS, then 20 LU below the mean
    // power, then the 95th minus the 10th percentile by rank.
    std::vector<double> abs;
    for (double l : shortTerm)
      if (l >= -70.0) abs.push_back(l);
    REQUIRE(!abs.empty());
    double power = 0.0;
    for (double l : abs)
      power += std::pow(10.0, l / 10.0);
    const double gate = 10.0 * std::log10(power / static_cast<double>(abs.size())) - 20.0;
    std::vector<double> rel;
    for (double l : abs)
      if (l >= gate) rel.push_back(l);
    std::sort(rel.begin(), rel.end());
    const double last = static_cast<double>(rel.size() - 1);
    const double want = rel[static_cast<std::size_t>(std::llround(last * 0.95))] -
                        rel[static_cast<std::size_t>(std::llround(last * 0.10))];
    CHECK(want > 5.0);
    CHECK(std::fabs(m.loudnessRange() - want) <= 0.02);
  }

  TEST_CASE("loudnessMeter: the loudness range waits for 3 s and starts over on reset() "
            "(#1237)") {
    ScopedSampleRate rate(48000);
    loudnessMeter m;
    CHECK(m.loudnessRange() == 0.f);
    Run(m, {{2.9, {-20.0}}});
    CHECK(m.loudnessRange() == 0.f); // no full short-term window yet
    Run(m, {{5.0, {-20.0}}, {5.0, {-30.0}}});
    CHECK(m.loudnessRange() > 5.f);
    m.reset();
    CHECK(m.loudnessRange() == 0.f);
    Run(m, {{6.0, {-75.0}}});
    CHECK(m.loudnessRange() == 0.f); // below the absolute gate
  }

  namespace {
    // A raised-cosine fade over the first and last `fade` samples.
    double Taper(std::size_t i, std::size_t length, std::size_t fade) {
      const std::size_t edge = std::min(i, length - 1 - i);
      if (edge >= fade) return 1.0;
      return 0.5 - 0.5 * std::cos(Pi * static_cast<double>(edge) / static_cast<double>(fade));
    }

    // Runs one signal through both channels of a stereo meter.
    float TruePeakOf(const std::vector<float>& x) {
      loudnessMeter m(2);
      for (std::size_t at = 0; at < x.size(); at += N) {
        const UInt len = static_cast<UInt>(std::min<std::size_t>(N, x.size() - at));
        buffer b(len);
        for (UInt i = 0; i < len; ++i)
          b.getPtr()[i] = x[at + i];
        buffer* pair[2] = {&b, &b};
        m(pair, 2);
      }
      CHECK(m.truePeak(1) == m.truePeak(2));
      return m.truePeak();
    }

    // Tech 3341 cases 15 to 19: a tapered sine of `fraction` fs.
    std::vector<float> TruePeakSine(double fraction, double amp, double degrees) {
      const std::size_t length = 24000; // 0.5 s at 48 kHz
      std::vector<float> x(length);
      for (std::size_t i = 0; i < length; ++i)
        x[i] = static_cast<float>(
            Taper(i, length, 480) * amp *
            std::sin(2.0 * Pi * fraction * static_cast<double>(i) + degrees * Pi / 180.0));
      return x;
    }

    // Tech 3341 cases 20 to 23, as Table 1 defines them: synthesised at
    // 4 fs, a sine of fs/6 at 0.50 FFS holding a single period of fs/4 at
    // 1.00, continuous in phase on both sides (each starts and ends at phase
    // 0), tapered with a short fade-in and fade-out, lowpass (anti-aliasing)
    // filtered and downsampled to fs from sample `offset` of the 4 fs rate.
    // Nothing is rescaled: the burst's amplitude of 1.00 is the expected
    // 0.0 dBTP. The spec leaves the lowpass open; this one is the textbook
    // anti-aliasing filter for the decimation, a Blackman-windowed sinc cut
    // off at the new Nyquist frequency, fs/2. Band-limiting alone takes the
    // burst's crest to about -0.13 dBFS at 4 fs; a lower cutoff takes more
    // (-0.25 dBFS at 20 kHz, where offset 2 reads -0.41 dBTP).
    std::vector<float> TruePeakBurst(std::size_t offset) {
      std::vector<double> hi;
      const std::size_t sixth = 24; // samples per period of fs/6 at 4 fs
      hi.reserve(2000 * sixth + 16);
      for (std::size_t i = 0; i < 1000 * sixth; ++i)
        hi.push_back(0.5 * std::sin(2.0 * Pi * static_cast<double>(i % sixth) / sixth));
      for (std::size_t i = 0; i < 16; ++i) // one period of fs/4 at 4 fs
        hi.push_back(std::sin(2.0 * Pi * static_cast<double>(i) / 16.0));
      for (std::size_t i = 0; i < 1000 * sixth; ++i)
        hi.push_back(0.5 * std::sin(2.0 * Pi * static_cast<double>(i % sixth) / sixth));
      for (std::size_t i = 0; i < hi.size(); ++i)
        hi[i] *= Taper(i, hi.size(), 1920); // 10 ms at 4 fs
      constexpr int half = 127;
      const double cut = 0.125; // fs/2 at 4 fs
      std::vector<double> h;
      for (int k = -half; k <= half; ++k) {
        const double sinc =
            k == 0 ? 2.0 * cut : std::sin(2.0 * Pi * cut * k) / (Pi * static_cast<double>(k));
        const double w =
            0.42 + 0.5 * std::cos(Pi * k / (half + 1)) + 0.08 * std::cos(2.0 * Pi * k / (half + 1));
        h.push_back(sinc * w);
      }
      std::vector<float> x;
      for (std::size_t n = offset; n < hi.size(); n += 4) {
        double y = 0.0;
        for (std::size_t j = 0; j < h.size(); ++j) {
          // Tap j weighs the sample j - half after n.
          const auto at = static_cast<std::ptrdiff_t>(n + half) - static_cast<std::ptrdiff_t>(j);
          if (at >= 0 && at < static_cast<std::ptrdiff_t>(hi.size()))
            y += h[j] * hi[static_cast<std::size_t>(at)];
        }
        x.push_back(static_cast<float>(y));
      }
      return x;
    }

    void WithinTruePeakTolerance(float got, double want) {
      CHECK(got <= want + 0.2);
      CHECK(got >= want - 0.4);
    }
  } // namespace

  TEST_CASE("loudnessMeter: EBU Tech 3341 cases 15 to 19, true peak of sines (#1237)") {
    ScopedSampleRate rate(48000);
    WithinTruePeakTolerance(TruePeakOf(TruePeakSine(0.25, 0.5, 0.0)), -6.0); // 15
    WithinTruePeakTolerance(TruePeakOf(TruePeakSine(0.25, 0.5, 45.0)), -6.0); // 16
    WithinTruePeakTolerance(TruePeakOf(TruePeakSine(1.0 / 6.0, 0.5, 60.0)), -6.0); // 17
    WithinTruePeakTolerance(TruePeakOf(TruePeakSine(0.125, 0.5, 67.5)), -6.0); // 18
    WithinTruePeakTolerance(TruePeakOf(TruePeakSine(0.25, 1.41, 45.0)), 3.0); // 19
    // Case 16's samples sit 3 dB below the waveform's crest: the sample
    // peak misses what the true peak finds.
    loudnessMeter m;
    const std::vector<float> x = TruePeakSine(0.25, 0.5, 45.0);
    buffer b(N);
    for (std::size_t at = 0; at + N <= x.size(); at += N) {
      for (UInt i = 0; i < N; ++i)
        b.getPtr()[i] = x[at + i];
      m(b);
    }
    CHECK(m.peak() == doctest::Approx(-9.03).epsilon(0.01));
    CHECK(m.truePeak() > m.peak() + 2.5f);
  }

  TEST_CASE("loudnessMeter: EBU Tech 3341 cases 20 to 23, a burst downsampled at four "
            "offsets (#1237, #1240)") {
    ScopedSampleRate rate(48000);
    for (std::size_t offset = 0; offset < 4; ++offset) {
      CAPTURE(offset);
      WithinTruePeakTolerance(TruePeakOf(TruePeakBurst(offset)), 0.0);
    }
  }

  TEST_CASE("loudnessMeter: the true peak is per channel, never below the sample peak, and "
            "starts over on reset() (#1237)") {
    ScopedSampleRate rate(48000);
    loudnessMeter m(2);
    CHECK(m.truePeak() == loudnessMeter::SILENCE);
    buffer a(N);
    buffer b(N);
    a = 0.f;
    b = 0.f;
    a.getPtr()[10] = 1.f; // an impulse: no interpolated sample reaches it
    b.getPtr()[20] = -0.25f;
    buffer* pair[2] = {&a, &b};
    m(pair, 2);
    CHECK(m.truePeak(1) == 0.f);
    CHECK(m.truePeak(1) >= m.peak(1));
    CHECK(m.truePeak(2) >= m.peak(2));
    CHECK(m.truePeak(2) == doctest::Approx(20.0 * std::log10(0.25)).epsilon(1e-4));
    CHECK(m.truePeak() == m.truePeak(1));
    CHECK(m.truePeak(9) == m.truePeak());
    m.reset();
    CHECK(m.truePeak() == loudnessMeter::SILENCE);
    // Nothing of the impulse is left in the interpolator after a reset.
    a = 0.f;
    m(a);
    CHECK(m.truePeak() == loudnessMeter::SILENCE);
  }

  TEST_CASE("loudnessMeter: reset() keeps the filters' memory, so a continuing signal reads "
            "no step (#1237)") {
    ScopedSampleRate rate(48000);
    loudnessMeter kept;
    loudnessMeter fresh;
    // A sine that is on its crest 1 s in, where the reset falls: had the
    // interpolator forgotten the samples before, it would see a step from
    // silence and overshoot.
    const std::vector<Segment> tone = {{1.0, {-6.0}}};
    Run(kept, tone, N, 1000.25);
    Run(fresh, tone, N, 1000.25);
    kept.reset(); // at a step boundary: 1 s is 10 steps
    // Run() starts the phase over; the second second continues from 1 s.
    std::vector<float> x(48000);
    for (std::size_t i = 0; i < x.size(); ++i)
      x[i] = static_cast<float>(
          std::pow(10.0, -6.0 / 20.0) *
          std::sin(2.0 * Pi * 1000.25 * static_cast<double>(i + 48000) / 48000.0));
    buffer b(N);
    for (std::size_t at = 0; at < x.size(); at += N) {
      for (UInt i = 0; i < N; ++i)
        b.getPtr()[i] = x[at + i];
      kept(b);
      fresh(b);
    }
    CHECK(kept.momentary() == doctest::Approx(fresh.momentary()).epsilon(1e-6));
    CHECK(kept.truePeak() <= fresh.truePeak());
    CHECK(kept.truePeak() == doctest::Approx(-6.0).epsilon(0.1 / 6.0));
  }

  // ---- The cases of EBU's loudness test set that are files, not
  // generators (issue #1240). The audio is never in the repository: set
  // YSE_EBU_LOUDNESS_DIR to a directory holding the test set (searched
  // recursively) and the cases whose files are there run; the others, and
  // all of them when the variable is unset, are skipped.

  namespace {
    std::string Lower(std::string s) {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    // The first file under `dir` whose name holds one of `names` (in order
    // of preference) not followed by another digit, so "seq-3341-2" never
    // takes "seq-3341-20"; prefixes such as "08_TC7_" and suffixes such as
    // "-24bit-v02" are allowed. Of several matches of one name the last in
    // order wins, the later version. Empty when there is none.
    std::filesystem::path FindEbuFile(const std::filesystem::path& dir,
                                      const std::vector<std::string>& names) {
      std::vector<std::filesystem::path> files;
      std::error_code ec;
      for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end;
           it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = Lower(it->path().extension().string());
        if (ext == ".wav" || ext == ".wave" || ext == ".flac") files.push_back(it->path());
      }
      std::sort(files.begin(), files.end());
      for (const std::string& name : names) {
        std::filesystem::path found;
        for (const std::filesystem::path& f : files) {
          const std::string file = Lower(f.filename().string());
          for (std::size_t at = file.find(name); at != std::string::npos;
               at = file.find(name, at + 1)) {
            const std::size_t after = at + name.size();
            if (after < file.size() && std::isdigit(static_cast<unsigned char>(file[after])))
              continue;
            found = f;
            break;
          }
        }
        if (!found.empty()) return found;
      }
      return {};
    }

    // Decodes every channel of `file` and runs it, then `silence` seconds
    // of silence, through a meter with one channel per file channel, at the
    // file's rate. False when the file does not decode.
    bool MeasureFile(const std::filesystem::path& file, double silence, Flt& integrated, Flt& range,
                     Flt& truePeak) {
      std::vector<std::unique_ptr<YSE::DSP::fileBuffer>> channels;
      const std::string path = file.string();
      while (channels.size() < loudnessMeter::MAX_CHANNELS) {
        auto b = std::make_unique<YSE::DSP::fileBuffer>(1);
        if (!b->load(path.c_str(), static_cast<UInt>(channels.size()))) break;
        channels.push_back(std::move(b));
      }
      if (channels.empty()) return false;
      const auto fileRate = static_cast<UInt>(std::lround(channels.front()->getFileSampleRate()));
      if (fileRate == 0) return false;
      ScopedSampleRate rate(fileRate);
      const auto count = static_cast<UInt>(channels.size());
      loudnessMeter m(count);
      const UInt frames = channels.front()->getLength();
      const UInt total = frames + static_cast<UInt>(std::llround(silence * fileRate));
      std::vector<buffer> bufs(count, buffer(N));
      std::vector<buffer*> ptrs;
      ptrs.reserve(count);
      for (buffer& b : bufs)
        ptrs.push_back(&b);
      for (UInt at = 0; at < total; at += N) {
        const UInt len = std::min(N, total - at);
        for (UInt c = 0; c < count; ++c) {
          if (bufs[c].getLength() != len) bufs[c].resize(len);
          const float* in = channels[c]->getPtr();
          const UInt have = channels[c]->getLength();
          for (UInt i = 0; i < len; ++i)
            bufs[c].getPtr()[i] = at + i < have ? in[at + i] : 0.f;
        }
        m(ptrs.data(), count);
      }
      integrated = m.integrated();
      range = m.loudnessRange();
      truePeak = m.truePeak();
      return true;
    }
  } // namespace

  TEST_CASE("loudnessMeter: EBU test set files from YSE_EBU_LOUDNESS_DIR, Tech 3341 cases "
            "7, 8 and 20 to 23 and Tech 3342 cases 5 and 6 (#1240)") {
    const char* dir = std::getenv("YSE_EBU_LOUDNESS_DIR");
    if (dir == nullptr || *dir == '\0' || !std::filesystem::is_directory(dir)) {
      MESSAGE("SKIPPED: YSE_EBU_LOUDNESS_DIR is not set to a directory, so the EBU test set "
              "files (Tech 3341 cases 7, 8, 20-23, Tech 3342 cases 5, 6) are not measured");
      return;
    }
    enum class What { Integrated, Range, TruePeak };
    struct FileCase {
      const char* name;
      std::vector<std::string> files;
      What what;
      double want;
      double below; // accepted below `want`
      double above; // accepted above `want`
    };
    // The EBU file names (seq-3341-7_seq-3342-5-24bit.wav holds both cases
    // 3341-7 and 3342-5, seq-3341-2011-8_seq-3342-6-24bit-v02.wav both
    // 3341-8 and 3342-6) and Table 1's expected values.
    const std::vector<FileCase> cases = {
        {"Tech 3341 case 7", {"seq-3341-7"}, What::Integrated, -23.0, 0.1, 0.1},
        {"Tech 3341 case 8", {"seq-3341-2011-8", "seq-3341-8"}, What::Integrated, -23.0, 0.1, 0.1},
        {"Tech 3342 case 5", {"seq-3342-5"}, What::Range, 5.0, 1.0, 1.0},
        {"Tech 3342 case 6", {"seq-3342-6"}, What::Range, 15.0, 1.0, 1.0},
        {"Tech 3341 case 20", {"seq-3341-20"}, What::TruePeak, 0.0, 0.4, 0.2},
        {"Tech 3341 case 21", {"seq-3341-21"}, What::TruePeak, 0.0, 0.4, 0.2},
        {"Tech 3341 case 22", {"seq-3341-22"}, What::TruePeak, 0.0, 0.4, 0.2},
        {"Tech 3341 case 23", {"seq-3341-23"}, What::TruePeak, 0.0, 0.4, 0.2},
    };
    for (const FileCase& c : cases) {
      const std::string name = c.name;
      CAPTURE(name);
      const std::filesystem::path file = FindEbuFile(dir, c.files);
      if (file.empty()) {
        const std::string why =
            "SKIPPED: " + name + ", no file named like *" + c.files.front() + "*.wav under " + dir;
        MESSAGE(why);
        continue;
      }
      CAPTURE(file.string());
      Flt integrated = 0.f;
      Flt range = 0.f;
      Flt truePeak = 0.f;
      // Tech 3342's reference code takes the loudness range after 1.5 s of
      // silence past a file's end.
      REQUIRE(MeasureFile(file, c.what == What::Range ? 1.5 : 0.0, integrated, range, truePeak));
      const double got =
          c.what == What::Integrated ? integrated : (c.what == What::Range ? range : truePeak);
      CHECK(got >= c.want - c.below);
      CHECK(got <= c.want + c.above);
    }
  }

  TEST_CASE("loudnessMeter: processing allocates nothing (#1129, #1237)") {
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
      for (int k = 0; k < 2000; ++k) { // 5.3 s: the loudness range fills too
        reports += m(pair, 2);
        if (k % 100 == 0) {
          (void)m.integrated();
          (void)m.maxMomentary();
          (void)m.loudnessRange();
          (void)m.peak(1);
          (void)m.truePeak(2);
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
