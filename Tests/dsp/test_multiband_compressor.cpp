// Tests for YSE::DSP::multibandCompressor (YseEngine/dsp/dynamics/
// multibandCompressor.hpp), issue #1117. The reference is the same chain
// built by hand from its pieces, DSP::crossover and DSP::MODULES::compressor:
// the cascade split, the allpass of every later crossover on each early band,
// the drive, one compressor per band, the levels and the output gain. The
// flatness of the split is checked in closed form: with nothing compressed
// the impulse response has a magnitude of 1 at every frequency.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <memory>
#include <vector>
#include "dsp/dynamics/multibandCompressor.hpp"
#include "dsp/filters/crossover.hpp"
#include "dsp/modules/compressor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::crossover;
using YSE::DSP::multibandCompressor;
using YSE::DSP::MODULES::compressor;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  float Lin(float dB) {
    return std::pow(10.f, dB / 20.f);
  }

  // Loud and quiet stretches of a few mixed sines, a little different per
  // channel.
  float Sample(unsigned int n, unsigned int channel) {
    const auto t = static_cast<double>(n) / 48000.0;
    const double level = (n / 3000) % 2 == 0 ? 0.9 : 0.1;
    const double c = static_cast<double>(channel);
    return static_cast<float>(level * (0.5 * std::sin(2.0 * PI * (70.0 + 10.0 * c) * t) +
                                       0.3 * std::sin(2.0 * PI * 900.0 * t) +
                                       0.2 * std::sin(2.0 * PI * (6000.0 - 500.0 * c) * t)));
  }

  MULTICHANNELBUFFER Make(unsigned int channels, unsigned int length) {
    MULTICHANNELBUFFER b(channels);
    for (buffer& c : b)
      c.resize(length);
    return b;
  }

  void Fill(MULTICHANNELBUFFER& b, unsigned int start) {
    for (unsigned int c = 0; c < b.size(); ++c)
      for (unsigned int i = 0; i < b[c].getLength(); ++i)
        b[c].getPtr()[i] = Sample(start + i, c);
  }

  // Settings for both the class and the reference.
  struct Settings {
    std::vector<float> freq;
    std::vector<float> threshold;
    std::vector<float> ratio;
    std::vector<float> attack;
    std::vector<float> release;
    std::vector<float> makeup;
    std::vector<float> drive;
    std::vector<float> level;
    float output = 0.f;
  };

  Settings Varied(unsigned int bands) {
    Settings s;
    for (unsigned int b = 0; b < bands; ++b) {
      const auto f = static_cast<float>(b);
      if (b + 1 < bands) s.freq.push_back(150.f * std::pow(4.f, f));
      s.threshold.push_back(-30.f + 3.f * f);
      s.ratio.push_back(2.f + f);
      s.attack.push_back(1.f + 2.f * f);
      s.release.push_back(40.f + 20.f * f);
      s.makeup.push_back(f);
      s.drive.push_back(3.f - f);
      s.level.push_back(-2.f + f);
    }
    s.output = -1.5f;
    return s;
  }

  void Apply(multibandCompressor& m, const Settings& s) {
    for (unsigned int k = 0; k < s.freq.size(); ++k)
      m.crossover(k, s.freq[k]);
    for (unsigned int b = 0; b < s.threshold.size(); ++b) {
      m.threshold(b, s.threshold[b]).ratio(b, s.ratio[b]).attack(b, s.attack[b]);
      m.release(b, s.release[b]).makeup(b, s.makeup[b]).drive(b, s.drive[b]);
      m.level(b, s.level[b]);
    }
    m.output(s.output);
  }

  // The chain written out from its pieces.
  struct Reference {
    unsigned int bands;
    unsigned int channels;
    Settings s;
    std::vector<std::vector<crossover>> split; // [channel][k]
    std::vector<std::vector<std::vector<crossover>>> allpass; // [channel][band][k]
    std::vector<std::unique_ptr<compressor>> comp;
    std::vector<MULTICHANNELBUFFER> band;

    Reference(unsigned int b, unsigned int c, Settings settings)
      : bands(b), channels(c), s(std::move(settings)) {
      split.assign(channels, std::vector<crossover>(bands - 1));
      allpass.assign(channels,
                     std::vector<std::vector<crossover>>(bands, std::vector<crossover>(bands)));
      for (unsigned int ch = 0; ch < channels; ++ch)
        for (unsigned int k = 0; k + 1 < bands; ++k) {
          split[ch][k].cutoff(s.freq[k]);
          for (unsigned int bb = 0; bb < bands; ++bb)
            allpass[ch][bb][k].cutoff(s.freq[k]);
        }
      for (unsigned int bb = 0; bb < bands; ++bb) {
        comp.push_back(std::make_unique<compressor>());
        compressor& k = *comp.back();
        k.threshold(s.threshold[bb]).ratio(s.ratio[bb]).attack(s.attack[bb]);
        k.release(s.release[bb]).makeup(s.makeup[bb]);
        band.push_back(Make(channels, N));
      }
    }

    // One block of at most N samples, in place.
    void Run(MULTICHANNELBUFFER& io) {
      const unsigned int length = io[0].getLength();
      for (MULTICHANNELBUFFER& b : band)
        for (buffer& c : b)
          c.resize(length);
      for (unsigned int ch = 0; ch < channels; ++ch)
        for (unsigned int i = 0; i < length; ++i) {
          float rest = io[ch].getPtr()[i];
          for (unsigned int k = 0; k + 1 < bands; ++k) {
            const crossover::outputs o = split[ch][k].process(rest);
            band[k][ch].getPtr()[i] = o.lowpass;
            rest = o.highpass;
          }
          band[bands - 1][ch].getPtr()[i] = rest;
          for (unsigned int bb = 0; bb + 2 < bands; ++bb) {
            float v = band[bb][ch].getPtr()[i];
            for (unsigned int k = bb + 1; k + 1 < bands; ++k) {
              const crossover::outputs o = allpass[ch][bb][k].process(v);
              v = o.lowpass + o.highpass;
            }
            band[bb][ch].getPtr()[i] = v;
          }
        }
      for (unsigned int bb = 0; bb < bands; ++bb) {
        const float drive = Lin(s.drive[bb]);
        if (drive != 1.f)
          for (buffer& c : band[bb])
            c *= drive;
        comp[bb]->process(band[bb]);
      }
      const float out = Lin(s.output);
      for (unsigned int ch = 0; ch < channels; ++ch)
        for (unsigned int i = 0; i < length; ++i) {
          float sum = 0.f;
          for (unsigned int bb = 0; bb < bands; ++bb)
            sum += band[bb][ch].getPtr()[i] * Lin(s.level[bb]);
          io[ch].getPtr()[i] = sum * out;
        }
    }
  };

  bool Same(MULTICHANNELBUFFER& a, MULTICHANNELBUFFER& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t c = 0; c < a.size(); ++c)
      if (!TestHelpers::buffersNearlyEqual(a[c], b[c], 0.f)) return false;
    return true;
  }

  // The magnitude of the DFT of `x` at `hz`.
  double Magnitude(const std::vector<float>& x, double hz, double rate) {
    std::complex<double> sum(0.0, 0.0);
    for (std::size_t n = 0; n < x.size(); ++n) {
      const double phase = -2.0 * PI * hz * static_cast<double>(n) / rate;
      sum += static_cast<double>(x[n]) * std::complex<double>(std::cos(phase), std::sin(phase));
    }
    return std::abs(sum);
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("multibandCompressor: defaults, ranges and settings out of range (#1117)") {
    multibandCompressor m;
    CHECK(m.bands() == 4);
    CHECK(m.channels() == 2);
    CHECK(m.crossover(0) == 200.f);
    CHECK(m.crossover(1) == 1000.f);
    CHECK(m.crossover(2) == 5000.f);
    CHECK(m.crossover(3) == 0.f);
    for (unsigned int b = 0; b < 4; ++b) {
      CHECK(m.threshold(b) == -18.f);
      CHECK(m.ratio(b) == 4.f);
      CHECK(m.attack(b) == 10.f);
      CHECK(m.release(b) == 100.f);
      CHECK(m.makeup(b) == 0.f);
      CHECK(m.drive(b) == 0.f);
      CHECK(m.level(b) == 0.f);
      CHECK(m.gainReduction(b) == 0.f);
    }
    CHECK(m.output() == 0.f);
    CHECK(m.detector() == YSE::DSP::MODULES::DETECT_PEAK);
    CHECK_FALSE(m.bypass());

    CHECK(multibandCompressor(1, 0).bands() == 2);
    CHECK(multibandCompressor(1, 0).channels() == 1);
    CHECK(multibandCompressor(9, 99).bands() == 5);
    CHECK(multibandCompressor(9, 99).channels() == multibandCompressor::MAX_CHANNELS);
    CHECK(multibandCompressor(2).crossover(0) == 1000.f);
    CHECK(multibandCompressor(3).crossover(1) == 3000.f);
    multibandCompressor five(5);
    CHECK(five.crossover(0) == 120.f);
    CHECK(five.crossover(3) == 8000.f);
    CHECK(multibandCompressor::DefaultCrossover(4, 3) == 0.f);
    CHECK(multibandCompressor::DefaultCrossover(6, 0) == 0.f);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    m.threshold(1, -100.f).ratio(1, 50.f).makeup(1, 40.f).attack(1, 0.f).release(1, 1e6f);
    CHECK(m.threshold(1) == -60.f);
    CHECK(m.ratio(1) == 20.f);
    CHECK(m.makeup(1) == 24.f);
    CHECK(m.attack(1) == doctest::Approx(0.1f));
    CHECK(m.release(1) == 2000.f);
    m.drive(2, -200.f).level(2, 99.f).output(-1000.f);
    CHECK(m.drive(2) == multibandCompressor::MIN_GAIN_DB);
    CHECK(m.level(2) == multibandCompressor::MAX_GAIN_DB);
    CHECK(m.output() == multibandCompressor::MIN_GAIN_DB);
    m.crossover(0, -5.f);
    CHECK(m.crossover(0) == 0.f);

    // Not finite, or out of range: ignored.
    m.crossover(1, nan).crossover(3, 50.f).drive(3, inf).level(4, 3.f).output(nan);
    m.threshold(0, nan).ratio(7, 2.f);
    CHECK(m.crossover(1) == 1000.f);
    CHECK(m.drive(3) == 0.f);
    CHECK(m.level(4) == 0.f);
    CHECK(m.output() == multibandCompressor::MIN_GAIN_DB);
    CHECK(m.threshold(0) == -18.f);
    CHECK(m.ratio(7) == 1.f);
    m.detector(YSE::DSP::MODULES::DETECT_RMS);
    CHECK(m.detector() == YSE::DSP::MODULES::DETECT_RMS);
  }

  TEST_CASE("multibandCompressor: with nothing compressed the bands add up flat, for 2 to 5 "
            "bands (#1117)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (unsigned int bands = 2; bands <= 5; ++bands) {
      CAPTURE(bands);
      multibandCompressor m(bands, 1);
      for (unsigned int b = 0; b < bands; ++b)
        m.ratio(b, 1.f);
      // The impulse response, 32768 samples.
      std::vector<float> response;
      MULTICHANNELBUFFER io = Make(1, N);
      for (unsigned int block = 0; block < 256; ++block) {
        io[0] = 0.f;
        if (block == 0) io[0].getPtr()[0] = 1.f;
        m.process(io);
        for (unsigned int i = 0; i < N; ++i)
          response.push_back(io[0].getPtr()[i]);
      }
      for (double hz :
           {20.0, 60.0, 120.0, 200.0, 500.0, 1000.0, 3000.0, 5000.0, 8000.0, 12000.0, 20000.0}) {
        CAPTURE(hz);
        CHECK(Magnitude(response, hz, 48000.0) == doctest::Approx(1.0).epsilon(1e-3));
      }
      // An allpass, not a wire: the phase does move.
      CHECK(std::abs(response[0] - 1.f) > 1e-3f);
    }
  }

  TEST_CASE("multibandCompressor: blocks against the chain built from its pieces, with every "
            "setting, 1 and 2 channels (#1117)") {
    TestHelpers::ScopedSampleRate rate(48000);
    for (unsigned int bands = 2; bands <= 5; ++bands) {
      for (unsigned int channels = 1; channels <= 2; ++channels) {
        CAPTURE(bands);
        CAPTURE(channels);
        const Settings s = Varied(bands);
        multibandCompressor m(bands, channels);
        Apply(m, s);
        Reference ref(bands, channels, s);
        MULTICHANNELBUFFER a = Make(channels, N);
        MULTICHANNELBUFFER b = Make(channels, N);
        float deepest = 0.f;
        for (unsigned int block = 0; block < 120; ++block) {
          Fill(a, block * N);
          Fill(b, block * N);
          m.process(a);
          ref.Run(b);
          REQUIRE(Same(a, b));
          for (unsigned int k = 0; k < bands; ++k) {
            CHECK(m.gainReduction(k) == std::min(0.f, ref.comp[k]->gainReductionDb()));
            deepest = std::min(deepest, m.gainReduction(k));
          }
        }
        CHECK(deepest < -3.f);
      }
    }
  }

  TEST_CASE("multibandCompressor: a loud tone is compressed in its own band only (#1117)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto run = [](double hz, multibandCompressor& m) {
      MULTICHANNELBUFFER io = Make(2, N);
      for (unsigned int block = 0; block < 200; ++block) {
        for (unsigned int i = 0; i < N; ++i) {
          const double t = static_cast<double>(block * N + i) / 48000.0;
          const auto v = static_cast<float>(std::sin(2.0 * PI * hz * t));
          io[0].getPtr()[i] = v;
          io[1].getPtr()[i] = v;
        }
        m.process(io);
      }
    };
    multibandCompressor low;
    run(50.0, low);
    CHECK(low.gainReduction(0) < -6.f);
    CHECK(low.gainReduction(2) > -0.5f);
    CHECK(low.gainReduction(3) > -0.5f);
    multibandCompressor high;
    run(12000.0, high);
    CHECK(high.gainReduction(3) < -6.f);
    CHECK(high.gainReduction(0) > -0.5f);
    CHECK(high.gainReduction(1) > -0.5f);
  }

  TEST_CASE("multibandCompressor: the channels of a band share one gain (#1117)") {
    TestHelpers::ScopedSampleRate rate(48000);
    multibandCompressor m(4, 2);
    m.threshold(0, -30.f).ratio(0, 8.f);
    MULTICHANNELBUFFER io = Make(2, N);
    double worst = 0.0;
    for (unsigned int block = 0; block < 100; ++block) {
      for (unsigned int i = 0; i < N; ++i) {
        const double t = static_cast<double>(block * N + i) / 48000.0;
        const auto v = static_cast<float>(0.9 * std::sin(2.0 * PI * 80.0 * t));
        io[0].getPtr()[i] = v;
        io[1].getPtr()[i] = 0.25f * v; // quiet: would not be compressed alone
      }
      m.process(io);
      for (unsigned int i = 0; i < N; ++i)
        worst = std::max(
            worst, static_cast<double>(std::abs(io[1].getPtr()[i] - 0.25f * io[0].getPtr()[i])));
    }
    CHECK(m.gainReduction(0) < -6.f);
    CHECK(worst < 1e-5);
  }

  TEST_CASE("multibandCompressor: bypass, non-finite input, and channels that do not fit "
            "(#1117)") {
    TestHelpers::ScopedSampleRate rate(48000);
    multibandCompressor m(3, 2);
    multibandCompressor ref(3, 2);
    MULTICHANNELBUFFER a = Make(2, N);
    MULTICHANNELBUFFER b = Make(2, N);
    MULTICHANNELBUFFER dry = Make(2, N);
    m.bypass(true);
    CHECK(m.bypass());
    for (unsigned int block = 0; block < 40; ++block) {
      Fill(a, block * N);
      Fill(b, block * N);
      Fill(dry, block * N);
      m.process(a);
      ref.process(b);
      CHECK(Same(a, dry)); // unchanged
      CHECK(m.gainReduction(0) == ref.gainReduction(0)); // still followed
    }
    CHECK(m.gainReduction(0) < -1.f);
    m.bypass(false);
    Fill(a, 40 * N);
    Fill(b, 40 * N);
    m.process(a);
    ref.process(b);
    CHECK(Same(a, b)); // no jump

    // A sample that is not finite counts as 0.
    multibandCompressor x(3, 1);
    multibandCompressor y(3, 1);
    MULTICHANNELBUFFER p = Make(1, N);
    MULTICHANNELBUFFER q = Make(1, N);
    Fill(p, 0);
    Fill(q, 0);
    p[0].getPtr()[5] = std::numeric_limits<float>::quiet_NaN();
    p[0].getPtr()[9] = std::numeric_limits<float>::infinity();
    q[0].getPtr()[5] = 0.f;
    q[0].getPtr()[9] = 0.f;
    x.process(p);
    y.process(q);
    CHECK(Same(p, q));

    // Too few channels, or channels of unequal length: left alone.
    multibandCompressor two(4, 2);
    MULTICHANNELBUFFER one = Make(1, N);
    Fill(one, 0);
    MULTICHANNELBUFFER copy = one;
    two.process(one);
    CHECK(Same(one, copy));
    MULTICHANNELBUFFER uneven(2);
    uneven[0].resize(N);
    uneven[1].resize(N / 2);
    Fill(uneven, 0);
    MULTICHANNELBUFFER unevenCopy = uneven;
    two.process(uneven);
    CHECK(Same(uneven, unevenCopy));
  }

  TEST_CASE("multibandCompressor: how the signal is cut into calls does not change it (#1117)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const unsigned int total = 40 * N + 37;
    const Settings s = Varied(5);
    const auto render = [&](const std::vector<unsigned int>& sizes) {
      multibandCompressor m(5, 2);
      Apply(m, s);
      std::vector<float> out;
      unsigned int at = 0;
      std::size_t next = 0;
      while (at < total) {
        const unsigned int size = std::min(sizes[next++ % sizes.size()], total - at);
        MULTICHANNELBUFFER io = Make(2, size);
        Fill(io, at);
        m.process(io);
        for (unsigned int i = 0; i < size; ++i) {
          out.push_back(io[0].getPtr()[i]);
          out.push_back(io[1].getPtr()[i]);
        }
        at += size;
      }
      return out;
    };
    const std::vector<float> blocks = render({N});
    CHECK(render({64, 13, 1}) == blocks);
    CHECK(render({3 * N + 17}) == blocks); // longer than STANDARD_BUFFERSIZE
    CHECK(render({total}) == blocks);
  }

  TEST_CASE("multibandCompressor: follows the live sample rate (#1117)") {
    MULTICHANNELBUFFER a = Make(2, N);
    MULTICHANNELBUFFER b = Make(2, N);
    std::unique_ptr<multibandCompressor> early;
    {
      TestHelpers::ScopedSampleRate rate(48000);
      early = std::make_unique<multibandCompressor>(4, 2);
    }
    TestHelpers::ScopedSampleRate rate(22050);
    multibandCompressor late(4, 2);
    for (unsigned int block = 0; block < 30; ++block) {
      Fill(a, block * N);
      Fill(b, block * N);
      early->process(a);
      late.process(b);
      REQUIRE(Same(a, b));
    }
    // And the crossovers really are at their frequencies at this rate:
    // nothing compressed, flat.
    multibandCompressor flat(4, 1);
    for (unsigned int k = 0; k < 4; ++k)
      flat.ratio(k, 1.f);
    std::vector<float> response;
    MULTICHANNELBUFFER io = Make(1, N);
    for (unsigned int block = 0; block < 128; ++block) {
      io[0] = 0.f;
      if (block == 0) io[0].getPtr()[0] = 1.f;
      flat.process(io);
      for (unsigned int i = 0; i < N; ++i)
        response.push_back(io[0].getPtr()[i]);
    }
    CHECK(Magnitude(response, 9000.0, 22050.0) == doctest::Approx(1.0).epsilon(1e-3));
  }

  TEST_CASE("multibandCompressor: processing and settings allocate nothing (#1117)") {
    TestHelpers::ScopedSampleRate rate(48000);
    multibandCompressor m(5, 2);
    MULTICHANNELBUFFER block = Make(2, N);
    MULTICHANNELBUFFER little = Make(2, 13);
    MULTICHANNELBUFFER big = Make(2, 3 * N + 5);
    Fill(block, 0);
    Fill(little, 0);
    Fill(big, 0);
    {
      TestHelpers::ProbeScope probe;
      m.process(block); // the first block: the constructor sized everything
      m.process(little);
      m.threshold(0, -40.f).ratio(1, 9.f).attack(2, 1.f).release(3, 30.f).makeup(4, 3.f);
      m.drive(0, 6.f).level(1, -3.f).output(2.f).crossover(2, 3000.f);
      m.detector(YSE::DSP::MODULES::DETECT_RMS).bypass(true);
      m.process(big);
      m.bypass(false);
      m.process(block);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
