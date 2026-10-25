// Tests for YSE::DSP::filterDesign (YseEngine/dsp/filters/filterDesign.hpp)
// and YSE::DSP::filterResponse (filterResponse.hpp), the filter design toolkit
// behind .filterdesign and .filterdetail (issue #1101).
//
// filterDesign is checked against closed forms that do not share its code:
// the magnitude of a bilinear-transformed Butterworth, Chebyshev type 1 and
// type 2 prototype at any frequency follows from the prototype's magnitude
// at the prewarped (and, for band filters, substituted) frequency, so the
// sections are evaluated here directly and compared with it for every
// response, a range of orders and edges. Plus published values (the
// second-order Butterworth halfband), the zeroes-poles-gain and transfer
// function forms agreeing with the sections, stability, section order,
// units and the live sample rate, failures and allocation freedom.
//
// filterResponse is checked on filters whose curves are known exactly
// (delays, a one-pole), against closed forms on designed filters, its group
// delay against the slope of its phase, and for allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>
#include "dsp/filters/biquadCascade.hpp"
#include "dsp/filters/filterDesign.hpp"
#include "dsp/filters/filterResponse.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::filterDesign;
using YSE::DSP::filterResponse;
using cplx = std::complex<double>;

namespace {
  constexpr double kPi = 3.14159265358979323846;

  using RESPONSE = filterDesign::RESPONSE;
  using TOPOLOGY = filterDesign::TOPOLOGY;
  using UNITS = filterDesign::UNITS;

  std::vector<double> Sections(const filterDesign& d) {
    std::vector<double> c(filterDesign::MAX_SECTIONS * 5);
    c.resize(d.coefficients(c.data(), static_cast<UInt>(c.size())));
    return c;
  }

  // H(e^jw) of the sections, evaluated directly.
  cplx Evaluate(const std::vector<double>& c, double w) {
    const cplx z1 = std::polar(1.0, -w);
    const cplx z2 = z1 * z1;
    cplx h(1.0, 0.0);
    for (std::size_t s = 0; s + 4 < c.size(); s += 5)
      h *= (c[s] + c[s + 1] * z1 + c[s + 2] * z2) / (1.0 + c[s + 3] * z1 + c[s + 4] * z2);
    return h;
  }

  // The Chebyshev polynomial T_n(x), for any real x.
  double Chebyshev(unsigned int n, double x) {
    const double a = std::fabs(x);
    const double t = a <= 1.0 ? std::cos(n * std::acos(a)) : std::cosh(n * std::acosh(a));
    return (x < 0.0 && n % 2 == 1) ? -t : t;
  }

  // |H|^2 of the analog prototype (cutoff 1) at frequency `v`.
  double Prototype(TOPOLOGY topology, unsigned int n, double v, double ripple, double attenuation) {
    v = std::fabs(v);
    switch (topology) {
    case TOPOLOGY::CHEBYSHEV_1: {
      const double eps2 = std::pow(10.0, 0.1 * ripple) - 1.0;
      const double t = Chebyshev(n, v);
      return 1.0 / (1.0 + eps2 * t * t);
    }
    case TOPOLOGY::CHEBYSHEV_2: {
      const double de2 = 1.0 / (std::pow(10.0, 0.1 * attenuation) - 1.0);
      if (v == 0.0) return 1.0; // T_n(infinity): the passband
      const double t = Chebyshev(n, 1.0 / v);
      return 1.0 / (1.0 + 1.0 / (de2 * t * t));
    }
    case TOPOLOGY::BUTTERWORTH:
    default:
      return 1.0 / (1.0 + std::pow(v, 2.0 * n));
    }
  }

  // |H|^2 the design should have at w (radians per sample), with edges w0
  // and w1 normalized to the Nyquist frequency.
  double Expected(RESPONSE response, TOPOLOGY topology, unsigned int order, double w0, double w1,
                  double w, double ripple, double attenuation) {
    // The bilinear transform at fs = 2: analog frequency 4 tan(w / 2).
    const auto warp = [](double normalized) { return 4.0 * std::tan(kPi * normalized / 2.0); };
    const double a = 4.0 * std::tan(w / 2.0);
    const bool band = response == RESPONSE::BANDPASS || response == RESPONSE::BANDSTOP;
    const unsigned int n = band ? (order + 1) / 2 : order;
    double v = 0.0;
    switch (response) {
    case RESPONSE::LOWPASS:
      v = a / warp(w0);
      break;
    case RESPONSE::HIGHPASS:
      v = warp(w0) / a;
      break;
    case RESPONSE::BANDPASS: {
      const double lo = warp(w0);
      const double hi = warp(w1);
      v = (a * a - lo * hi) / ((hi - lo) * a);
      break;
    }
    case RESPONSE::BANDSTOP: {
      const double lo = warp(w0);
      const double hi = warp(w1);
      v = ((hi - lo) * a) / (lo * hi - a * a);
      break;
    }
    }
    return Prototype(topology, n, v, ripple, attenuation);
  }

  const char* Name(RESPONSE r) {
    switch (r) {
    case RESPONSE::LOWPASS:
      return "lowpass";
    case RESPONSE::HIGHPASS:
      return "highpass";
    case RESPONSE::BANDPASS:
      return "bandpass";
    default:
      return "bandstop";
    }
  }

  const char* Name(TOPOLOGY t) {
    switch (t) {
    case TOPOLOGY::BUTTERWORTH:
      return "butterworth";
    case TOPOLOGY::CHEBYSHEV_1:
      return "chebyshev-1";
    default:
      return "chebyshev-2";
    }
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("filterDesign: the defaults, and nothing designed before design() (#1101)") {
    filterDesign d;
    CHECK(d.response() == RESPONSE::LOWPASS);
    CHECK(d.topology() == TOPOLOGY::BUTTERWORTH);
    CHECK(d.order() == 2);
    CHECK(d.frequencyAt(0) == 1000.f);
    CHECK(d.frequencyAt(1) == 2000.f);
    CHECK(d.units() == UNITS::HERTZ);
    CHECK(d.sampleRate() == 0.f);
    CHECK(d.passbandRipple() == 1.f);
    CHECK(d.stopbandAttenuation() == 40.f);
    CHECK(d.sections() == 0);
    CHECK(d.poles() == 0);
  }

  TEST_CASE("filterDesign: the second-order Butterworth halfband lowpass has its published "
            "coefficients (#1101)") {
    // b = [1, 2, 1] / (2 + sqrt 2), a = [1, 0, (2 - sqrt 2) / (2 + sqrt 2)]:
    // 0.29289322 0.58578644 0.29289322, 1 0 0.17157288.
    filterDesign d;
    d.units(UNITS::NORMALIZED).frequency(0.5f);
    REQUIRE(d.design());
    REQUIRE(d.sections() == 1);
    REQUIRE(d.poles() == 2);
    const double g = 1.0 / (2.0 + std::sqrt(2.0));
    const auto c = Sections(d);
    CHECK(c[0] == doctest::Approx(g).epsilon(1e-12));
    CHECK(c[1] == doctest::Approx(2.0 * g).epsilon(1e-12));
    CHECK(c[2] == doctest::Approx(g).epsilon(1e-12));
    CHECK(std::fabs(c[3]) < 1e-12);
    CHECK(c[4] == doctest::Approx((2.0 - std::sqrt(2.0)) * g).epsilon(1e-12));
    CHECK(d.numerator(0) == doctest::Approx(g).epsilon(1e-12));
    CHECK(d.numerator(1) == doctest::Approx(2.0 * g).epsilon(1e-12));
    CHECK(d.numerator(2) == doctest::Approx(g).epsilon(1e-12));
    CHECK(d.denominator(0) == 1.0);
    CHECK(std::fabs(d.denominator(1)) < 1e-12);
    CHECK(d.denominator(2) == doctest::Approx((2.0 - std::sqrt(2.0)) * g).epsilon(1e-12));

    // First order: (1 + z^-1) / 2.
    d.order(1);
    REQUIRE(d.design());
    REQUIRE(d.sections() == 1);
    const auto first = Sections(d);
    CHECK(first[0] == doctest::Approx(0.5).epsilon(1e-12));
    CHECK(first[1] == doctest::Approx(0.5).epsilon(1e-12));
    CHECK(first[2] == 0.0);
    CHECK(std::fabs(first[3]) < 1e-12);
    CHECK(first[4] == 0.0);
  }

  TEST_CASE("filterDesign: every response and topology follows its prototype's closed form "
            "(#1101)") {
    const RESPONSE responses[] = {RESPONSE::LOWPASS, RESPONSE::HIGHPASS, RESPONSE::BANDPASS,
                                  RESPONSE::BANDSTOP};
    const TOPOLOGY topologies[] = {TOPOLOGY::BUTTERWORTH, TOPOLOGY::CHEBYSHEV_1,
                                   TOPOLOGY::CHEBYSHEV_2};
    const unsigned int orders[] = {1, 2, 3, 4, 5, 8, 12};
    const double edges[][2] = {{0.05, 0.3}, {0.25, 0.5}, {0.6, 0.9}};
    const double ripples[] = {0.5, 3.0};
    const double attenuations[] = {30.0, 60.0};
    for (const RESPONSE response : responses)
      for (const TOPOLOGY topology : topologies)
        for (const unsigned int order : orders)
          for (const auto& edge : edges)
            for (int variant = 0; variant < 2; ++variant) {
              CAPTURE(Name(response));
              CAPTURE(Name(topology));
              CAPTURE(order);
              CAPTURE(edge[0]);
              CAPTURE(edge[1]);
              const double ripple = ripples[variant];
              const double attenuation = attenuations[variant];
              filterDesign d;
              d.response(response)
                  .topology(topology)
                  .order(order)
                  .units(UNITS::NORMALIZED)
                  .frequency(static_cast<float>(edge[0]), static_cast<float>(edge[1]))
                  .passbandRipple(static_cast<float>(ripple))
                  .stopbandAttenuation(static_cast<float>(attenuation));
              REQUIRE(d.design());
              const bool band = response == RESPONSE::BANDPASS || response == RESPONSE::BANDSTOP;
              const unsigned int poles = band ? 2 * ((order + 1) / 2) : order;
              CHECK(d.poles() == poles);
              CHECK(d.sections() == (poles + 1) / 2);
              // The frequencies as the design read them (from floats).
              const double w0 = static_cast<double>(static_cast<float>(edge[0]));
              const double w1 = static_cast<double>(static_cast<float>(edge[1]));
              const auto c = Sections(d);
              bool close = true;
              for (int i = 1; i < 200; ++i) {
                const double w = kPi * i / 200.0;
                const double got = std::norm(Evaluate(c, w));
                const double want =
                    Expected(response, topology, order, w0, w1, w, ripple, attenuation);
                if (std::fabs(got - want) > 1e-8 + 1e-6 * want) {
                  close = false;
                  CAPTURE(w);
                  CAPTURE(got);
                  CAPTURE(want);
                  CHECK(close);
                  break;
                }
              }
              CHECK(close);
            }
  }

  TEST_CASE("filterDesign: the edges mean -3 dB, the passband ripple and the stopband "
            "attenuation (#1101)") {
    const auto gainAt = [](const filterDesign& d, double normalized) {
      return std::abs(Evaluate(Sections(d), kPi * normalized));
    };
    filterDesign d;
    d.units(UNITS::NORMALIZED).frequency(0.25f).order(6);
    REQUIRE(d.design());
    CHECK(gainAt(d, 0.25) == doctest::Approx(std::sqrt(0.5)).epsilon(1e-6));
    CHECK(gainAt(d, 0.0) == doctest::Approx(1.0).epsilon(1e-9));

    d.topology(TOPOLOGY::CHEBYSHEV_1).passbandRipple(2.f);
    REQUIRE(d.design());
    CHECK(gainAt(d, 0.25) == doctest::Approx(std::pow(10.0, -2.0 / 20.0)).epsilon(1e-6));
    // Even order: the gain at DC is at the bottom of the ripple.
    CHECK(gainAt(d, 0.0) == doctest::Approx(std::pow(10.0, -2.0 / 20.0)).epsilon(1e-6));

    d.topology(TOPOLOGY::CHEBYSHEV_2).stopbandAttenuation(50.f);
    REQUIRE(d.design());
    CHECK(gainAt(d, 0.25) == doctest::Approx(std::pow(10.0, -50.0 / 20.0)).epsilon(1e-5));
    CHECK(gainAt(d, 0.0) == doctest::Approx(1.0).epsilon(1e-9));
    // Nowhere in the stopband above the attenuation.
    for (int i = 0; i < 100; ++i) {
      const double f = 0.25 + 0.75 * i / 100.0;
      CHECK(gainAt(d, f) <= std::pow(10.0, -50.0 / 20.0) * (1.0 + 1e-6));
    }

    // A bandpass passes its center, a bandstop stops it.
    d.topology(TOPOLOGY::BUTTERWORTH).response(RESPONSE::BANDPASS).frequency(0.2f, 0.4f);
    REQUIRE(d.design());
    const double center =
        2.0 / kPi * std::atan(std::sqrt(std::tan(kPi * 0.1) * std::tan(kPi * 0.2)));
    CHECK(gainAt(d, center) == doctest::Approx(1.0).epsilon(1e-9));
    CHECK(gainAt(d, 0.2) == doctest::Approx(std::sqrt(0.5)).epsilon(1e-6));
    CHECK(gainAt(d, 0.4) == doctest::Approx(std::sqrt(0.5)).epsilon(1e-6));
    d.response(RESPONSE::BANDSTOP);
    REQUIRE(d.design());
    CHECK(gainAt(d, center) < 1e-6);
    CHECK(gainAt(d, 0.0) == doctest::Approx(1.0).epsilon(1e-9));
  }

  TEST_CASE("filterDesign: zeroes, poles, gain and the transfer function agree with the "
            "sections; every design up to the highest order is stable (#1101)") {
    const RESPONSE responses[] = {RESPONSE::LOWPASS, RESPONSE::HIGHPASS, RESPONSE::BANDPASS,
                                  RESPONSE::BANDSTOP};
    const TOPOLOGY topologies[] = {TOPOLOGY::BUTTERWORTH, TOPOLOGY::CHEBYSHEV_1,
                                   TOPOLOGY::CHEBYSHEV_2};
    for (const RESPONSE response : responses)
      for (const TOPOLOGY topology : topologies)
        for (unsigned int order = 1; order <= filterDesign::MAX_ORDER; ++order) {
          CAPTURE(Name(response));
          CAPTURE(Name(topology));
          CAPTURE(order);
          filterDesign d;
          d.response(response)
              .topology(topology)
              .order(order)
              .units(UNITS::NORMALIZED)
              .frequency(0.3f, 0.5f);
          REQUIRE(d.design());
          const unsigned int poles = d.poles();
          REQUIRE(poles >= order);
          REQUIRE(poles <= filterDesign::MAX_ORDER);
          // Stable, and the sections' poles are the design's.
          bool stable = true;
          for (unsigned int i = 0; i < poles; ++i)
            if (!(std::abs(d.pole(i)) < 1.0)) stable = false;
          CHECK(stable);

          const auto c = Sections(d);
          REQUIRE(c.size() == 5 * static_cast<std::size_t>(d.sections()));
          bool sectionsStable = true;
          for (std::size_t s = 0; s < c.size(); s += 5) {
            // |roots| < 1 for 1 + b1 z^-1 + b2 z^-2: |b2| < 1 and |b1| < 1 + b2.
            if (!(std::fabs(c[s + 4]) < 1.0 && std::fabs(c[s + 3]) < 1.0 + c[s + 4]))
              sectionsStable = false;
          }
          CHECK(sectionsStable);

          // The three forms are one filter.
          bool same = true;
          for (int i = 1; i < 40; ++i) {
            const double w = kPi * i / 40.0;
            const cplx z1 = std::polar(1.0, -w);
            const cplx hs = Evaluate(c, w);
            cplx hz = d.gain();
            for (unsigned int k = 0; k < poles; ++k)
              hz *= (1.0 - d.zero(k) * z1) / (1.0 - d.pole(k) * z1);
            if (std::abs(hs - hz) > 1e-7 * std::max(1.0, std::abs(hz))) same = false;
            if (order <= 8) {
              cplx num = 0.0;
              cplx den = 0.0;
              cplx zk = 1.0;
              for (unsigned int k = 0; k <= poles; ++k) {
                num += d.numerator(k) * zk;
                den += d.denominator(k) * zk;
                zk *= z1;
              }
              if (std::abs(hs - num / den) > 1e-6 * std::max(1.0, std::abs(hz))) same = false;
            }
          }
          CHECK(same);
          CHECK(d.denominator(0) == 1.0);
          CHECK(d.numerator(poles + 1) == 0.0);
          CHECK(d.pole(poles) == cplx());
        }
  }

  TEST_CASE("filterDesign: an odd order puts the first-order section first, the sharpest "
            "resonance last (#1101)") {
    filterDesign d;
    d.units(UNITS::NORMALIZED).frequency(0.3f).order(7).topology(TOPOLOGY::CHEBYSHEV_1);
    REQUIRE(d.design());
    REQUIRE(d.sections() == 4);
    const auto c = Sections(d);
    CHECK(c[2] == 0.0);
    CHECK(c[4] == 0.0);
    // The pole radius of each second-order section, sqrt(b2), grows.
    for (std::size_t s = 5; s + 5 < c.size(); s += 5)
      CHECK(std::sqrt(c[s + 4]) < std::sqrt(c[s + 9]));
    // Even orders: second-order sections only.
    d.order(8);
    REQUIRE(d.design());
    for (const double v : Sections(d))
      CHECK(std::isfinite(v));
    CHECK(Sections(d)[4] != 0.0);
  }

  TEST_CASE("filterDesign: units, the order of band edges and the order clamp (#1101)") {
    TestHelpers::ScopedSampleRate rate(48000);
    filterDesign hz;
    hz.response(RESPONSE::BANDPASS).order(4).frequency(2400.f, 7200.f);
    REQUIRE(hz.design());
    filterDesign normalized = hz;
    normalized.units(UNITS::NORMALIZED).frequency(0.1f, 0.3f);
    REQUIRE(normalized.design());
    filterDesign radians = hz;
    radians.units(UNITS::RADIANS)
        .frequency(static_cast<float>(0.1 * kPi), static_cast<float>(0.3 * kPi));
    REQUIRE(radians.design());
    filterDesign swapped = hz;
    swapped.frequency(7200.f, 2400.f);
    REQUIRE(swapped.design());
    const auto a = Sections(hz);
    const auto b = Sections(normalized);
    const auto c = Sections(radians);
    const auto e = Sections(swapped);
    REQUIRE(a.size() == b.size());
    REQUIRE(a.size() == c.size());
    REQUIRE(a == e);
    for (std::size_t i = 0; i < a.size(); ++i) {
      CHECK(a[i] == doctest::Approx(b[i]).epsilon(1e-6));
      CHECK(a[i] == doctest::Approx(c[i]).epsilon(1e-5));
    }

    // An explicit sample rate.
    filterDesign fixed = hz;
    fixed.sampleRate(96000.f).frequency(4800.f, 14400.f);
    REQUIRE(fixed.design());
    const auto f = Sections(fixed);
    for (std::size_t i = 0; i < a.size(); ++i)
      CHECK(a[i] == doctest::Approx(f[i]).epsilon(1e-6));

    filterDesign clamp;
    clamp.order(0);
    CHECK(clamp.order() == 1);
    clamp.order(1000);
    CHECK(clamp.order() == filterDesign::MAX_ORDER);
  }

  TEST_CASE("filterDesign: hertz follow the engine's sample rate when design() runs (#1101)") {
    filterDesign d;
    d.frequency(6000.f).order(3);
    std::vector<double> at48;
    {
      TestHelpers::ScopedSampleRate rate(48000);
      REQUIRE(d.design());
      at48 = Sections(d);
    }
    {
      TestHelpers::ScopedSampleRate rate(24000);
      REQUIRE(d.design());
      const auto at24 = Sections(d);
      REQUIRE(at24.size() == at48.size());
      CHECK(at24 != at48);
      // 6000 Hz at 24 kHz is half the Nyquist frequency.
      filterDesign half;
      half.units(UNITS::NORMALIZED).frequency(0.5f).order(3);
      REQUIRE(half.design());
      const auto h = Sections(half);
      for (std::size_t i = 0; i < h.size(); ++i)
        CHECK(at24[i] == doctest::Approx(h[i]).epsilon(1e-9));
    }
  }

  TEST_CASE("filterDesign: frequencies outside 0 .. Nyquist and equal band edges fail; "
            "invalid settings are ignored (#1101)") {
    TestHelpers::ScopedSampleRate rate(48000);
    filterDesign d;
    REQUIRE(d.design());
    REQUIRE(d.sections() == 1);
    const float bad[] = {0.f, -100.f, 24000.f, 30000.f};
    for (const float f : bad) {
      CAPTURE(f);
      d.frequency(f);
      CHECK_FALSE(d.design());
      CHECK(d.sections() == 0);
      CHECK(d.poles() == 0);
      CHECK(d.section(0).a0 == 0.f);
      d.frequency(1000.f);
      CHECK(d.design());
    }
    // A band uses the second edge too; a lowpass does not.
    d.frequency(1000.f, 30000.f);
    CHECK(d.design());
    d.response(RESPONSE::BANDSTOP);
    CHECK_FALSE(d.design());
    d.frequency(1000.f, 1000.f);
    CHECK_FALSE(d.design());
    d.frequency(1000.f, 1500.f);
    CHECK(d.design());

    // No sample rate at all: hertz cannot be read.
    {
      TestHelpers::ScopedSampleRate none(0);
      CHECK_FALSE(d.design());
    }

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    d.frequency(nan).frequency(inf, nan);
    CHECK(d.frequencyAt(0) == 1000.f);
    CHECK(d.frequencyAt(1) == 1500.f);
    d.sampleRate(-1.f).sampleRate(nan);
    CHECK(d.sampleRate() == 0.f);
    d.passbandRipple(0.f).passbandRipple(-3.f).passbandRipple(inf);
    CHECK(d.passbandRipple() == 1.f);
    d.stopbandAttenuation(0.f).stopbandAttenuation(nan);
    CHECK(d.stopbandAttenuation() == 40.f);
    // coefficients() writes whole sections only.
    REQUIRE(d.design());
    float out[7] = {};
    CHECK(d.coefficients(out, 7) == 5);
    CHECK(d.coefficients(static_cast<float*>(nullptr), 7) == 0);
  }

  TEST_CASE("filterDesign: the sections run in biquadCascade filter as designed (#1101)") {
    // A sine in the passband passes, one in the stopband does not.
    filterDesign d;
    d.units(UNITS::NORMALIZED).frequency(0.1f).order(8).topology(TOPOLOGY::CHEBYSHEV_2);
    d.stopbandAttenuation(60.f);
    REQUIRE(d.design());
    float c[filterDesign::MAX_SECTIONS * 5];
    const UInt n = d.coefficients(c, filterDesign::MAX_SECTIONS * 5);
    const auto level = [&](double w) {
      YSE::DSP::biquadCascade cascade;
      cascade.coefficients(c, n);
      double peak = 0.0;
      for (int i = 0; i < 20000; ++i) {
        const float y = cascade.process(static_cast<float>(std::sin(w * i)));
        if (i > 15000) peak = std::max(peak, std::fabs(static_cast<double>(y)));
      }
      return peak;
    };
    CHECK(level(0.02 * kPi) == doctest::Approx(1.0).epsilon(0.01));
    CHECK(level(0.3 * kPi) < 0.0011);
  }

  TEST_CASE("filterDesign: design() allocates nothing (#1101)") {
    filterDesign d;
    d.response(RESPONSE::BANDSTOP).topology(TOPOLOGY::CHEBYSHEV_2).order(31);
    d.units(UNITS::NORMALIZED).frequency(0.2f, 0.6f);
    float out[filterDesign::MAX_SECTIONS * 5];
    {
      TestHelpers::ProbeScope probe;
      CHECK(d.design());
      CHECK(d.coefficients(out, filterDesign::MAX_SECTIONS * 5) == 80);
      d.response(RESPONSE::LOWPASS).topology(TOPOLOGY::CHEBYSHEV_1).order(32);
      CHECK(d.design());
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("filterResponse: a delay of one and of two samples (#1101)") {
    filterResponse r(64);
    CHECK(r.points() == 64);
    CHECK(r.capacity() == 64);
    const float one[] = {0.f, 1.f, 0.f, 0.f, 0.f};
    REQUIRE(r.analyse(one, 5));
    CHECK(r.sections() == 1);
    for (UInt i = 0; i < 64; ++i) {
      CAPTURE(i);
      const double w = kPi * i / 64.0;
      CHECK(r.frequency(i) == doctest::Approx(w));
      CHECK(r.magnitude()[i] == doctest::Approx(1.0).epsilon(1e-6));
      CHECK(r.phase()[i] == doctest::Approx(-w).epsilon(1e-6));
      CHECK(r.groupDelay()[i] == doctest::Approx(1.0).epsilon(1e-6));
      CHECK(r.phaseDelay()[i] == doctest::Approx(1.0).epsilon(1e-6));
      CHECK(r.impulse()[i] == (i == 1 ? 1.f : 0.f));
      CHECK(r.step()[i] == (i >= 1 ? 1.f : 0.f));
    }
    // Two samples: the phase passes -pi and keeps going (unwrapped).
    const float two[] = {0.f, 0.f, 1.f, 0.f, 0.f};
    REQUIRE(r.analyse(two, 5));
    for (UInt i = 0; i < 64; ++i) {
      CAPTURE(i);
      const double w = kPi * i / 64.0;
      CHECK(r.phase()[i] == doctest::Approx(-2.0 * w).epsilon(1e-6));
      CHECK(r.groupDelay()[i] == doctest::Approx(2.0).epsilon(1e-6));
      CHECK(r.phaseDelay()[i] == doctest::Approx(2.0).epsilon(1e-6));
    }
    // Two sections in series: three samples, gain 0.5 * 3.
    const float series[] = {0.f, 0.5f, 0.f, 0.f, 0.f, 0.f, 0.f, 3.f, 0.f, 0.f};
    REQUIRE(r.analyse(series, 10));
    CHECK(r.sections() == 2);
    CHECK(r.impulse()[3] == 1.5f);
    CHECK(r.magnitude()[10] == doctest::Approx(1.5).epsilon(1e-6));
    CHECK(r.groupDelay()[10] == doctest::Approx(3.0).epsilon(1e-6));
  }

  TEST_CASE("filterResponse: a one-pole lowpass against its closed forms (#1101)") {
    // y(n) = x(n) + 0.5 y(n-1).
    filterResponse r(128);
    const float c[] = {1.f, 0.f, 0.f, -0.5f, 0.f};
    REQUIRE(r.analyse(c, 5));
    const double p = 0.5;
    for (UInt i = 0; i < 128; ++i) {
      CAPTURE(i);
      const double w = kPi * i / 128.0;
      const cplx h = 1.0 / (1.0 - p * std::polar(1.0, -w));
      CHECK(r.magnitude()[i] == doctest::Approx(std::abs(h)).epsilon(1e-6));
      CHECK(r.phase()[i] == doctest::Approx(std::arg(h)).epsilon(1e-6));
      const double gd = (p * std::cos(w) - p * p) / (1.0 - 2.0 * p * std::cos(w) + p * p);
      CHECK(r.groupDelay()[i] == doctest::Approx(gd).epsilon(1e-6));
      if (i > 0) CHECK(r.phaseDelay()[i] == doctest::Approx(-std::arg(h) / w).epsilon(1e-6));
      CHECK(r.impulse()[i] == doctest::Approx(std::pow(p, i)).epsilon(1e-6));
      CHECK(r.step()[i] == doctest::Approx(2.0 * (1.0 - std::pow(p, i + 1))).epsilon(1e-6));
    }
    CHECK(r.phaseDelay()[0] == r.groupDelay()[0]);
  }

  TEST_CASE("filterResponse: a designed filter, its group delay as the slope of its phase, a "
            "zero on the unit circle (#1101)") {
    filterDesign d;
    d.units(UNITS::NORMALIZED).frequency(0.3f).order(6).topology(TOPOLOGY::CHEBYSHEV_1);
    REQUIRE(d.design());
    filterResponse r;
    CHECK(r.points() == filterResponse::DEFAULT_POINTS);
    REQUIRE(r.analyse(d));
    CHECK(r.sections() == 3);
    for (UInt i = 1; i + 1 < r.points(); ++i) {
      CAPTURE(i);
      const double w = r.frequency(i);
      const double want =
          Expected(RESPONSE::LOWPASS, TOPOLOGY::CHEBYSHEV_1, 6, 0.3, 0.3, w, 1.0, 40.0);
      CHECK(static_cast<double>(r.magnitude()[i]) ==
            doctest::Approx(std::sqrt(want)).epsilon(1e-5));
      // -d phase / d w by central differences.
      const double step = kPi / r.points();
      const double slope = -(r.phase()[i + 1] - r.phase()[i - 1]) / (2.0 * step);
      CHECK(static_cast<double>(r.groupDelay()[i]) ==
            doctest::Approx(slope).epsilon(0.02).scale(1.0));
    }
    // The impulse response sums to the gain at DC, the step response settles
    // there.
    double sum = 0.0;
    for (UInt i = 0; i < r.points(); ++i)
      sum += r.impulse()[i];
    CHECK(sum == doctest::Approx(r.magnitude()[0]).epsilon(1e-4));
    CHECK(r.step()[r.points() - 1] == doctest::Approx(r.magnitude()[0]).epsilon(1e-4));

    // The float list gives the same curves to float precision.
    float c[filterDesign::MAX_SECTIONS * 5];
    filterResponse fromList;
    REQUIRE(fromList.analyse(c, d.coefficients(c, filterDesign::MAX_SECTIONS * 5)));
    for (UInt i = 0; i < r.points(); ++i)
      CHECK(fromList.magnitude()[i] == doctest::Approx(r.magnitude()[i]).epsilon(1e-4));

    // A highpass has its zeroes at DC: magnitude 0 there and a finite group
    // delay (half the degree of each section's numerator).
    d.response(RESPONSE::HIGHPASS).topology(TOPOLOGY::BUTTERWORTH).order(2);
    REQUIRE(d.design());
    REQUIRE(r.analyse(d));
    CHECK(r.magnitude()[0] < 1e-9f);
    CHECK(std::isfinite(r.groupDelay()[0]));
    CHECK(std::isfinite(r.phaseDelay()[0]));
  }

  TEST_CASE("filterResponse: points, and lists it does not analyse (#1101)") {
    filterResponse r(32);
    r.points(8);
    CHECK(r.points() == 8);
    r.points(0);
    CHECK(r.points() == 1);
    r.points(1000);
    CHECK(r.points() == 32);
    CHECK(filterResponse(0).capacity() == 1);

    r.points(8);
    const float delay[] = {0.f, 1.f, 0.f, 0.f, 0.f, 9.f};
    REQUIRE(r.analyse(delay, 6)); // the incomplete set is ignored
    CHECK(r.sections() == 1);
    CHECK(r.frequency(4) == doctest::Approx(kPi / 2.0));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float bad[] = {1.f, 0.f, 0.f, nan, 0.f};
    CHECK_FALSE(r.analyse(bad, 5));
    CHECK_FALSE(r.analyse(delay, 4));
    CHECK_FALSE(r.analyse(static_cast<const float*>(nullptr), 5));
    CHECK_FALSE(r.analyse(filterDesign()));
    // Unchanged by the refusals.
    CHECK(r.sections() == 1);
    CHECK(r.impulse()[1] == 1.f);
  }

  TEST_CASE("filterResponse: analyse() allocates nothing (#1101)") {
    filterDesign d;
    d.units(UNITS::NORMALIZED).response(RESPONSE::BANDPASS).frequency(0.1f, 0.2f).order(16);
    REQUIRE(d.design());
    filterResponse r;
    float c[filterDesign::MAX_SECTIONS * 5];
    const UInt n = d.coefficients(c, filterDesign::MAX_SECTIONS * 5);
    {
      TestHelpers::ProbeScope probe;
      CHECK(r.analyse(d));
      r.points(100);
      CHECK(r.analyse(c, n));
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
