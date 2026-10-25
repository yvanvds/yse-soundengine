// Tests for YSE::DSP::filterBands (YseEngine/dsp/filters/filterBands.hpp),
// the filter editor state behind ~filtergraph (issue #1102).
//
// The coefficients of a band in a filter mode are filterCoefficients'
// (tested in test_filter_coeff.cpp), so they are checked against it, and
// the cascade's response against closed forms of the cookbook filters (a
// lowpass has gain Q at its frequency, a peak its gain, a shelf its gain at
// one end and 1 at the other, an allpass a flat gain and half a turn of
// phase at its frequency), against filterResponse, and against a sine
// filtered through biquadCascade. Plus display mode, cascades, constraints,
// flat, the live sample rate, invalid input and allocation freedom.

#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/filters/biquadCascade.hpp"
#include "dsp/filters/filterBands.hpp"
#include "dsp/filters/filterCoefficients.hpp"
#include "dsp/filters/filterResponse.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::biquadCascade;
using YSE::DSP::buffer;
using YSE::DSP::filterBands;
using YSE::DSP::filterCoefficients;
using YSE::DSP::filterResponse;

namespace {
  constexpr double kPi = 3.14159265358979323846;

  bool Same(const filterBands::coefficientSet& a, const filterBands::coefficientSet& b) {
    return a.a0 == b.a0 && a.a1 == b.a1 && a.a2 == b.a2 && a.b1 == b.b1 && a.b2 == b.b2;
  }

  float Amplitude(const filterBands& bands, float hz, float rate = 0.f) {
    float amplitude = 0.f;
    float phase = 0.f;
    REQUIRE(bands.responseAt(hz, amplitude, phase, rate));
    return amplitude;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("filterBands: a band in a filter mode has filterCoefficients' coefficients at the "
            "live sample rate (#1102)") {
    TestHelpers::ScopedSampleRate rate(48000);
    filterBands bands;
    CHECK(bands.bands() == 1);
    CHECK(bands.mode(0) == filterBands::LOWPASS);
    CHECK(bands.frequency(0) == 1000.f);
    CHECK(bands.gain(0) == 1.f);
    CHECK(bands.q(0) == filterBands::FLAT_Q);
    CHECK(Same(bands.coefficients(0),
               filterCoefficients::Compute(filterCoefficients::LOWPASS, 1000.f, 1.f,
                                           filterBands::FLAT_Q, 48000.f)));

    // Each mode is the filterCoefficients type of the same number.
    for (int m = filterBands::LOWPASS; m <= filterBands::ALLPASS; ++m) {
      CAPTURE(m);
      bands.mode(0, static_cast<filterBands::MODE>(m)).parameters(0, 2500.f, 2.f, 3.f);
      const int type = m == filterBands::ALLPASS ? filterCoefficients::GAINAPASS : m;
      CHECK(Same(bands.coefficients(0),
                 filterCoefficients::Compute(static_cast<filterCoefficients::TYPE>(type), 2500.f,
                                             2.f, 3.f, 48000.f)));
    }

    // With the gain enabled the five plain types read it.
    const int plain[] = {filterBands::LOWPASS, filterBands::HIGHPASS, filterBands::BANDPASS,
                         filterBands::BANDSTOP, filterBands::RESONANT};
    const int gained[] = {filterCoefficients::GAINLPASS, filterCoefficients::GAINHPASS,
                          filterCoefficients::GAINBPASS, filterCoefficients::GAINBSTOP,
                          filterCoefficients::GAINRESONANT};
    bands.gainEnabled(0, true);
    for (int i = 0; i < 5; ++i) {
      CAPTURE(i);
      bands.mode(0, static_cast<filterBands::MODE>(plain[i]));
      CHECK(Same(bands.coefficients(0),
                 filterCoefficients::Compute(static_cast<filterCoefficients::TYPE>(gained[i]),
                                             2500.f, 2.f, 3.f, 48000.f)));
    }
    CHECK(bands.gainEnabled(0));

    // The live rate is read at the call, and an explicit one overrides it.
    const filterBands::coefficientSet at48 = bands.coefficients(0);
    {
      TestHelpers::ScopedSampleRate lower(24000);
      CHECK_FALSE(Same(bands.coefficients(0), at48));
      CHECK(Same(bands.coefficients(0, 48000.f), at48));
    }
    CHECK(Same(bands.coefficients(0), at48));
  }

  TEST_CASE("filterBands: the response follows the cookbook's closed forms (#1102)") {
    TestHelpers::ScopedSampleRate rate(48000);
    filterBands bands;

    // A lowpass has gain Q at its frequency and 1 at DC; gain enabled, 2Q
    // and 2.
    bands.parameters(0, 1000.f, 2.f, 4.f);
    CHECK(Amplitude(bands, 1000.f) == doctest::Approx(4.0).epsilon(1e-4));
    CHECK(Amplitude(bands, 0.f) == doctest::Approx(1.0).epsilon(1e-5));
    bands.gainEnabled(0, true);
    CHECK(Amplitude(bands, 1000.f) == doctest::Approx(8.0).epsilon(1e-4));
    CHECK(Amplitude(bands, 0.f) == doctest::Approx(2.0).epsilon(1e-5));

    // A bandpass has gain 1 at its frequency (the gain with it enabled).
    bands.mode(0, filterBands::BANDPASS);
    CHECK(Amplitude(bands, 1000.f) == doctest::Approx(2.0).epsilon(1e-4));
    bands.gainEnabled(0, false);
    CHECK(Amplitude(bands, 1000.f) == doctest::Approx(1.0).epsilon(1e-4));

    // A peak has its gain at its frequency, 1 far away.
    bands.mode(0, filterBands::PEAKNOTCH).parameters(0, 3000.f, 4.f, 2.f);
    CHECK(Amplitude(bands, 3000.f) == doctest::Approx(4.0).epsilon(1e-4));
    CHECK(Amplitude(bands, 0.f) == doctest::Approx(1.0).epsilon(1e-4));

    // A low shelf has its gain at DC and 1 at Nyquist, a high shelf the
    // other way round.
    bands.mode(0, filterBands::LOWSHELF).parameters(0, 500.f, 4.f, 1.f);
    CHECK(Amplitude(bands, 0.f) == doctest::Approx(4.0).epsilon(1e-4));
    CHECK(Amplitude(bands, 24000.f) == doctest::Approx(1.0).epsilon(1e-4));
    bands.mode(0, filterBands::HIGHSHELF);
    CHECK(Amplitude(bands, 0.f) == doctest::Approx(1.0).epsilon(1e-4));
    CHECK(Amplitude(bands, 24000.f) == doctest::Approx(4.0).epsilon(1e-4));

    // An allpass has a flat gain, its own, and half a turn at its frequency.
    bands.mode(0, filterBands::ALLPASS).parameters(0, 2000.f, 0.5f, 1.f);
    for (const float hz : {0.f, 100.f, 2000.f, 9000.f, 23000.f})
      CHECK(Amplitude(bands, hz) == doctest::Approx(0.5).epsilon(1e-4));
    float amplitude = 0.f;
    float phase = 0.f;
    REQUIRE(bands.responseAt(2000.f, amplitude, phase));
    CHECK(std::fabs(phase) == doctest::Approx(kPi).epsilon(1e-4));

    // Two bands multiply their gains and add their phases.
    filterBands one;
    one.mode(0, filterBands::PEAKNOTCH).parameters(0, 3000.f, 2.f, 1.f);
    filterBands two;
    two.mode(0, filterBands::HIGHPASS).parameters(0, 800.f, 1.f, 2.f);
    filterBands both;
    both.bands(2)
        .mode(0, filterBands::PEAKNOTCH)
        .parameters(0, 3000.f, 2.f, 1.f)
        .mode(1, filterBands::HIGHPASS)
        .parameters(1, 800.f, 1.f, 2.f);
    for (const float hz : {50.f, 800.f, 2000.f, 3000.f, 12000.f}) {
      CAPTURE(hz);
      float a1 = 0.f, p1 = 0.f, a2 = 0.f, p2 = 0.f, a = 0.f, p = 0.f;
      REQUIRE(one.responseAt(hz, a1, p1));
      REQUIRE(two.responseAt(hz, a2, p2));
      REQUIRE(both.responseAt(hz, a, p));
      CHECK(a == doctest::Approx(a1 * a2).epsilon(1e-5));
      CHECK(std::cos(p) == doctest::Approx(std::cos(p1 + p2)).epsilon(1e-4));
      CHECK(std::sin(p) == doctest::Approx(std::sin(p1 + p2)).epsilon(1e-4));
    }
  }

  TEST_CASE("filterBands: response() agrees with responseAt and a filtered sine (#1102)") {
    TestHelpers::ScopedSampleRate rate(48000);
    filterBands bands;
    bands.bands(3)
        .mode(0, filterBands::LOWSHELF)
        .parameters(0, 200.f, 2.f, 1.f)
        .mode(1, filterBands::PEAKNOTCH)
        .parameters(1, 2500.f, 0.25f, 3.f)
        .mode(2, filterBands::LOWPASS)
        .parameters(2, 9000.f, 1.f, 0.9f);

    filterResponse curves(64);
    REQUIRE(bands.response(curves));
    CHECK(curves.sections() == 3);
    for (UInt i = 0; i < curves.points(); ++i) {
      CAPTURE(i);
      const float hz = static_cast<float>(curves.frequency(i) * 48000.0 / (2.0 * kPi));
      CHECK(curves.magnitude()[i] == doctest::Approx(Amplitude(bands, hz)).epsilon(1e-4));
    }

    // A 2500 Hz sine through biquadCascade, past the transient, has the
    // amplitude responseAt predicts.
    float c[filterBands::MAX_BANDS * 5];
    const UInt n = bands.sections(c, filterBands::MAX_BANDS * 5);
    REQUIRE(n == 15);
    biquadCascade cascade(filterBands::MAX_BANDS);
    cascade.coefficients(c, n);
    const unsigned int length = YSE::STANDARD_BUFFERSIZE;
    buffer in(length);
    double peak = 0.0;
    unsigned int t = 0;
    for (int block = 0; block < 200; ++block) {
      for (unsigned int i = 0; i < length; ++i, ++t)
        in.getPtr()[i] = static_cast<float>(std::sin(2.0 * kPi * 2500.0 * t / 48000.0));
      buffer& out = cascade(in);
      if (block >= 150)
        for (unsigned int i = 0; i < length; ++i)
          peak = std::max(peak, static_cast<double>(std::fabs(out.getPtr()[i])));
    }
    // The largest sample lies within a tenth of a sample of the true peak
    // (19.2 samples per period): within 0.11 % of it.
    CHECK(peak == doctest::Approx(Amplitude(bands, 2500.f)).epsilon(3e-3));
  }

  TEST_CASE("filterBands: display mode keeps the coefficients it was given (#1102)") {
    TestHelpers::ScopedSampleRate rate(48000);
    filterBands bands;
    bands.mode(0, filterBands::PEAKNOTCH).parameters(0, 1500.f, 3.f, 2.f);
    const filterBands::coefficientSet before = bands.coefficients(0);

    // Into display: the band keeps sounding as it did; settings no longer
    // change it.
    bands.mode(0, filterBands::DISPLAY);
    CHECK(bands.mode(0) == filterBands::DISPLAY);
    CHECK(Same(bands.coefficients(0), before));
    bands.parameters(0, 300.f, 1.f, 1.f);
    CHECK(Same(bands.coefficients(0), before));
    CHECK(bands.frequency(0) == 300.f);

    // One coefficient at a time, or all five; a value that is not finite
    // is ignored.
    bands.coefficient(0, 0, 0.5f).coefficient(0, 4, 0.25f).coefficient(0, 5, 9.f);
    bands.coefficient(0, 1, std::nanf(""));
    filterBands::coefficientSet expect = before;
    expect.a0 = 0.5f;
    expect.b2 = 0.25f;
    CHECK(Same(bands.coefficients(0), expect));
    bands.coefficients(0, filterBands::coefficientSet{1.f, 0.f, 0.f, 0.f, 0.f});
    CHECK(Amplitude(bands, 4000.f) == doctest::Approx(1.0));
    bands.coefficients(0, filterBands::coefficientSet{1.f, INFINITY, 0.f, 0.f, 0.f});
    CHECK(bands.coefficients(0).a1 == 0.f);

    // Display again does not recapture; back to a filter mode recomputes.
    bands.mode(0, filterBands::DISPLAY);
    CHECK(bands.coefficients(0).a0 == 1.f);
    bands.mode(0, filterBands::LOWPASS);
    CHECK(Same(bands.coefficients(0),
               filterCoefficients::Compute(filterCoefficients::LOWPASS, 300.f, 1.f, 1.f, 48000.f)));
  }

  TEST_CASE("filterBands: a cascade sets display bands and the count (#1102)") {
    filterBands bands;
    const float list[] = {1.f, 2.f, 3.f, 4.f, 5.f, 0.5f, 0.f, 0.f, -0.5f, 0.f, 9.f, 9.f};
    CHECK(bands.cascade(list, 12) == 2);
    CHECK(bands.bands() == 2);
    CHECK(bands.mode(0) == filterBands::DISPLAY);
    CHECK(bands.mode(1) == filterBands::DISPLAY);
    CHECK(Same(bands.coefficients(0), filterBands::coefficientSet{1.f, 2.f, 3.f, 4.f, 5.f}));
    CHECK(Same(bands.coefficients(1), filterBands::coefficientSet{0.5f, 0.f, 0.f, -0.5f, 0.f}));
    float out[15];
    REQUIRE(bands.sections(out, 15) == 10);
    for (int i = 0; i < 10; ++i)
      CHECK(out[i] == list[i]);
    // Capacity for whole bands only.
    CHECK(bands.sections(out, 9) == 5);

    // Without a whole set, or with a value that is not finite, nothing.
    CHECK(bands.cascade(list, 4) == 0);
    const float bad[] = {1.f, 0.f, NAN, 0.f, 0.f};
    CHECK(bands.cascade(bad, 5) == 0);
    CHECK(bands.cascade(nullptr, 5) == 0);
    CHECK(bands.bands() == 2);

    // At most MAX_BANDS.
    std::vector<float> many(5 * 30, 0.f);
    for (std::size_t i = 0; i < many.size(); i += 5)
      many[i] = 1.f;
    CHECK(bands.cascade(many.data(), static_cast<UInt>(many.size())) == filterBands::MAX_BANDS);
    CHECK(bands.bands() == filterBands::MAX_BANDS);
  }

  TEST_CASE("filterBands: constraints clamp the settings; flat; bandwidth (#1102)") {
    filterBands bands;
    bands.parameters(0, 5000.f, 8.f, 20.f);
    // Bounds, a min above its max swapped, 0 for none; the current values
    // are clamped at once.
    bands.constraints(0, 100.f, 2000.f, 4.f, 0.5f, 0.f, 10.f);
    CHECK(bands.constraint(0, 0) == 100.f);
    CHECK(bands.constraint(0, 1) == 2000.f);
    CHECK(bands.constraint(0, 2) == 0.5f);
    CHECK(bands.constraint(0, 3) == 4.f);
    CHECK(bands.constraint(0, 4) == 0.f);
    CHECK(bands.constraint(0, 5) == 10.f);
    CHECK(bands.frequency(0) == 2000.f);
    CHECK(bands.gain(0) == 4.f);
    CHECK(bands.q(0) == 10.f);
    bands.parameters(0, 20.f, 0.1f, 0.001f);
    CHECK(bands.frequency(0) == 100.f);
    CHECK(bands.gain(0) == 0.5f);
    CHECK(bands.q(0) == 0.001f); // no lower bound on Q
    // A bound that is not finite leaves them all as they were.
    bands.constraints(0, NAN, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(bands.constraint(0, 0) == 100.f);
    bands.constraints(0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    bands.frequency(0, 20000.f);
    CHECK(bands.frequency(0) == 20000.f);

    // Bandwidth: frequency over the Q, the Q clamped as the filter clamps it.
    bands.parameters(0, 1200.f, 1.f, 4.f);
    CHECK(bands.bandwidth(0) == doctest::Approx(300.f));
    bands.q(0, 0.f);
    CHECK(bands.bandwidth(0) == doctest::Approx(1200.f / filterCoefficients::MIN_Q));

    // flat: lowpass and highpass Q and gain, bandpass and bandstop nothing,
    // the rest gain only; within the constraints.
    bands.parameters(0, 1000.f, 3.f, 5.f).flat(0);
    CHECK(bands.q(0) == filterBands::FLAT_Q);
    CHECK(bands.gain(0) == 1.f);
    bands.mode(0, filterBands::BANDPASS).parameters(0, 1000.f, 3.f, 5.f).flat(0);
    CHECK(bands.gain(0) == 3.f);
    CHECK(bands.q(0) == 5.f);
    bands.mode(0, filterBands::PEAKNOTCH).flat(0);
    CHECK(bands.gain(0) == 1.f);
    CHECK(bands.q(0) == 5.f);
    bands.constraints(0, 0.f, 0.f, 2.f, 0.f, 0.f, 0.f).flat(0);
    CHECK(bands.gain(0) == 2.f);
  }

  TEST_CASE("filterBands: counts, indices and values out of range are ignored (#1102)") {
    filterBands bands;
    CHECK(bands.bands(0).bands() == 1);
    CHECK(bands.bands(100).bands() == filterBands::MAX_BANDS);
    bands.bands(2);
    // Bands past the count keep their settings for when it grows.
    bands.frequency(5, 777.f);
    bands.bands(6);
    CHECK(bands.frequency(5) == 777.f);

    const filterBands before = bands;
    bands.frequency(filterBands::MAX_BANDS, 1.f)
        .mode(filterBands::MAX_BANDS, filterBands::ALLPASS)
        .mode(0, static_cast<filterBands::MODE>(10))
        .mode(0, static_cast<filterBands::MODE>(-1))
        .frequency(0, NAN)
        .gain(0, INFINITY)
        .q(0, NAN);
    CHECK(bands.mode(0) == before.mode(0));
    CHECK(bands.frequency(0) == before.frequency(0));
    CHECK(bands.gain(0) == before.gain(0));
    CHECK(bands.q(0) == before.q(0));
    CHECK(bands.frequency(filterBands::MAX_BANDS) == 1000.f);
    CHECK_FALSE(bands.handle(filterBands::MAX_BANDS));
    bands.handle(1, true);
    CHECK(bands.handle(1));

    float amplitude = 0.f;
    float phase = 0.f;
    CHECK_FALSE(bands.responseAt(NAN, amplitude, phase));
    // A rate not above 0 is the live one.
    CHECK(bands.responseAt(100.f, amplitude, phase, -1.f));
    CHECK(bands.sections(nullptr, 10) == 0);
  }

  TEST_CASE("filterBands: settings, coefficients and the response allocate nothing (#1102)") {
    TestHelpers::ScopedSampleRate rate(48000);
    filterBands bands;
    filterResponse curves(256);
    float out[filterBands::MAX_BANDS * 5];
    const float list[] = {1.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 0.5f, 0.f, 0.f, 0.f};
    {
      TestHelpers::ProbeScope probe;
      bands.bands(4)
          .mode(1, filterBands::PEAKNOTCH)
          .parameters(1, 2000.f, 2.f, 1.f)
          .constraints(1, 100.f, 5000.f, 0.f, 0.f, 0.f, 0.f)
          .flat(1)
          .mode(2, filterBands::DISPLAY);
      const filterBands copy = bands;
      float amplitude = 0.f;
      float phase = 0.f;
      CHECK(copy.responseAt(1000.f, amplitude, phase));
      CHECK(bands.sections(out, filterBands::MAX_BANDS * 5) == 20);
      CHECK(bands.response(curves));
      CHECK(bands.cascade(list, 10) == 2);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
