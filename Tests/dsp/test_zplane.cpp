// Tests for the pole/zero conversions of DSP::filterDesign
// (sectionRoots / sectionFromRoots) and for DSP::zPlane, issue #1103: roots
// of closed-form sections, the roots of a designed filter, a resonator moved
// into place on the z plane against its closed-form impulse response, the
// editing rules, cascades, invalid input and allocation freedom.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/filters/biquadCascade.hpp"
#include "dsp/filters/filterDesign.hpp"
#include "dsp/filters/zPlane.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::biquadCascade;
using YSE::DSP::buffer;
using YSE::DSP::filterDesign;
using YSE::DSP::zPlane;
using cplx = std::complex<double>;

namespace {
  constexpr double kPi = 3.14159265358979323846;

  bool Near(cplx a, cplx b, double tolerance = 1e-6) {
    return std::abs(a - b) <= tolerance;
  }

  // The gain and phase of one section, from its coefficients, at w radians
  // per sample.
  cplx Response(const zPlane::coefficientSet& c, double w) {
    const cplx e1 = std::exp(cplx(0.0, -w));
    const cplx e2 = e1 * e1;
    return (static_cast<double>(c.a0) + static_cast<double>(c.a1) * e1 +
            static_cast<double>(c.a2) * e2) /
           (1.0 + static_cast<double>(c.b1) * e1 + static_cast<double>(c.b2) * e2);
  }

  // The same from its roots and gain.
  cplx Response(double gain, cplx z0, cplx z1, cplx p0, cplx p1, double w) {
    const cplx e1 = std::exp(cplx(0.0, -w));
    return gain * (1.0 - z0 * e1) * (1.0 - z1 * e1) / ((1.0 - p0 * e1) * (1.0 - p1 * e1));
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("filterDesign: sectionRoots and sectionFromRoots convert between a section and its "
            "roots (#1103)") {
    // Zeroes 0.9 e^(+-i pi/3), poles 0.5 +- 0.5i, gain 2: closed-form
    // coefficients.
    const cplx z(0.9 * std::cos(kPi / 3.0), 0.9 * std::sin(kPi / 3.0));
    const cplx p(0.5, 0.5);
    const zPlane::coefficientSet c =
        filterDesign::sectionFromRoots(2.0, z, std::conj(z), p, std::conj(p));
    CHECK(c.a0 == 2.f);
    CHECK(c.a1 == doctest::Approx(-2.0 * 2.0 * z.real()));
    CHECK(c.a2 == doctest::Approx(2.0 * 0.81));
    CHECK(c.b1 == doctest::Approx(-1.0));
    CHECK(c.b2 == doctest::Approx(0.5));

    cplx z0;
    cplx z1;
    cplx p0;
    cplx p1;
    REQUIRE(filterDesign::sectionRoots(c, z0, z1, p0, p1));
    // A complex pair, upper half first.
    CHECK(Near(z0, z));
    CHECK(Near(z1, std::conj(z)));
    CHECK(Near(p0, p));
    CHECK(Near(p1, std::conj(p)));
    // The roots and the coefficients are the same filter.
    for (int k = 0; k < 10; ++k) {
      const double w = 0.3 * k;
      CHECK(Near(Response(c, w), Response(c.a0, z0, z1, p0, p1, w), 1e-5));
    }

    // Real roots, the larger first.
    const zPlane::coefficientSet r = filterDesign::sectionFromRoots(0.5, -0.25, 0.5, -0.3, 0.8);
    CHECK(r.a1 == doctest::Approx(-0.5 * 0.25));
    CHECK(r.a2 == doctest::Approx(0.5 * -0.125));
    CHECK(r.b1 == doctest::Approx(-0.5));
    CHECK(r.b2 == doctest::Approx(-0.24));
    REQUIRE(filterDesign::sectionRoots(r, z0, z1, p0, p1));
    CHECK(Near(z0, 0.5));
    CHECK(Near(z1, -0.25));
    CHECK(Near(p0, 0.8));
    CHECK(Near(p1, -0.3));

    // A double root, and a first-order section: a root at the origin.
    REQUIRE(filterDesign::sectionRoots({1.f, -1.f, 0.25f, -0.6f, 0.f}, z0, z1, p0, p1));
    CHECK(Near(z0, 0.5));
    CHECK(Near(z1, 0.5));
    CHECK(Near(p0, 0.6));
    CHECK(Near(p1, 0.0));
    REQUIRE(filterDesign::sectionRoots({1.f, 0.7f, 0.f, 0.f, 0.f}, z0, z1, p0, p1));
    CHECK(Near(z0, 0.0));
    CHECK(Near(z1, -0.7));
    CHECK(Near(p0, 0.0));
    CHECK(Near(p1, 0.0));

    // a0 = 0: no pair of finite zeroes; the poles are still read.
    CHECK_FALSE(filterDesign::sectionRoots({0.f, 1.f, 0.5f, 0.f, -0.25f}, z0, z1, p0, p1));
    CHECK(z0 == cplx());
    CHECK(z1 == cplx());
    CHECK(Near(p0, 0.5));
    CHECK(Near(p1, -0.5));
    CHECK_FALSE(filterDesign::sectionRoots({1.f, NAN, 0.f, 0.f, 0.f}, z0, z1, p0, p1));
    CHECK(p0 == cplx());
  }

  TEST_CASE("zPlane: a designed filter's sections give back its zeroes and poles (#1103)") {
    filterDesign design;
    design.topology(filterDesign::TOPOLOGY::CHEBYSHEV_2)
        .response(filterDesign::RESPONSE::BANDPASS)
        .order(8)
        .units(filterDesign::UNITS::NORMALIZED)
        .frequency(0.2f, 0.4f);
    REQUIRE(design.design());
    float list[filterDesign::MAX_SECTIONS * 5];
    const UInt n = design.coefficients(list, filterDesign::MAX_SECTIONS * 5);
    REQUIRE(n == 20);

    zPlane plane;
    REQUIRE(plane.cascade(list, n) == 4);
    REQUIRE(plane.order() == design.poles());
    // Each designed root is one of the plane's, within float rounding of the
    // coefficients.
    for (UInt i = 0; i < design.poles(); ++i) {
      CAPTURE(i);
      double nearestZero = 1e9;
      double nearestPole = 1e9;
      for (UInt j = 0; j < plane.order(); ++j) {
        nearestZero = std::min(nearestZero, std::abs(plane.zero(j) - design.zero(i)));
        nearestPole = std::min(nearestPole, std::abs(plane.pole(j) - design.pole(i)));
      }
      CHECK(nearestZero < 1e-3);
      CHECK(nearestPole < 1e-4);
    }
    // The gains multiply to the design's.
    double product = 1.0;
    for (UInt s = 0; s < plane.sections(); ++s)
      product *= plane.gain(s);
    CHECK(product == doctest::Approx(design.gain()).epsilon(1e-5));
  }

  TEST_CASE("zPlane: a pole pair moved to r e^(+-i theta) rings as r^n sin((n+1) theta) / "
            "sin(theta) (#1103)") {
    const double radius = 0.95;
    const double theta = 0.3;
    zPlane plane;
    // From the identity: both poles at the origin. Moving one off the real
    // axis brings its partner along as its conjugate.
    REQUIRE(plane.movePole(1, radius * std::cos(theta), -radius * std::sin(theta)));
    CHECK(Near(plane.pole(0), std::polar(radius, theta), 1e-6));
    CHECK(Near(plane.pole(1), std::polar(radius, -theta), 1e-6));
    const zPlane::coefficientSet c = plane.coefficients(0);
    CHECK(c.a0 == 1.f);
    CHECK(c.a1 == 0.f);
    CHECK(c.a2 == 0.f);
    CHECK(c.b1 == doctest::Approx(-2.0 * radius * std::cos(theta)));
    CHECK(c.b2 == doctest::Approx(radius * radius));

    float list[5];
    REQUIRE(plane.coefficients(list, 5) == 5);
    biquadCascade cascade(1);
    cascade.coefficients(list, 5);
    const unsigned int length = YSE::STANDARD_BUFFERSIZE;
    buffer in(length);
    unsigned int t = 0;
    for (int block = 0; block < 4; ++block) {
      for (unsigned int i = 0; i < length; ++i)
        in.getPtr()[i] = (block == 0 && i == 0) ? 1.f : 0.f;
      buffer& out = cascade(in);
      for (unsigned int i = 0; i < length; ++i, ++t) {
        const double expect = std::pow(radius, t) * std::sin((t + 1) * theta) / std::sin(theta);
        CHECK(out.getPtr()[i] == doctest::Approx(expect).epsilon(1e-3).scale(1.0));
      }
    }

    // A notch: the zeroes on the unit circle at theta cancel that frequency.
    REQUIRE(plane.moveZero(0, std::cos(theta), std::sin(theta)));
    CHECK(Near(plane.zero(1), std::polar(1.0, -theta), 1e-6));
    CHECK(std::abs(Response(plane.coefficients(0), theta)) < 1e-4);
    CHECK(std::abs(Response(plane.coefficients(0), 0.0)) > 0.1);
  }

  TEST_CASE("zPlane: moving roots keeps the coefficients real; pconstrain keeps poles inside the "
            "unit circle (#1103)") {
    zPlane plane;
    plane.coefficients(0, filterDesign::sectionFromRoots(3.0, 0.5, -0.25, 0.8, -0.3));

    // A real root onto the real axis leaves its real partner.
    REQUIRE(plane.movePole(0, 0.6, 0.0));
    CHECK(Near(plane.pole(0), 0.6, 1e-6));
    CHECK(Near(plane.pole(1), -0.3, 1e-6));
    // The zeroes and the gain stay.
    CHECK(plane.gain(0) == 3.f);
    CHECK(Near(plane.zero(0), 0.5, 1e-6));
    CHECK(Near(plane.zero(1), -0.25, 1e-6));

    // Off the axis, the partner becomes the conjugate; back onto it, the
    // conjugate comes along to the same point.
    REQUIRE(plane.moveZero(1, 0.1, 0.7));
    CHECK(Near(plane.zero(0), cplx(0.1, 0.7), 1e-6));
    CHECK(Near(plane.zero(1), cplx(0.1, -0.7), 1e-6));
    REQUIRE(plane.moveZero(0, -0.4, 0.0));
    CHECK(Near(plane.zero(0), -0.4, 1e-3));
    CHECK(Near(plane.zero(1), -0.4, 1e-3));
    CHECK(plane.gain(0) == 3.f);

    // pconstrain: a pole outside is pulled onto MAX_POLE_RADIUS; zeroes are
    // free.
    CHECK_FALSE(plane.constrainPoles());
    REQUIRE(plane.movePole(0, 0.0, 2.0));
    CHECK(Near(plane.pole(0), cplx(0.0, 2.0), 1e-5));
    plane.constrainPoles(true);
    // Turning it on leaves the poles where they are.
    CHECK(Near(plane.pole(0), cplx(0.0, 2.0), 1e-5));
    REQUIRE(plane.movePole(1, 3.0, -4.0));
    CHECK(std::abs(plane.pole(0)) == doctest::Approx(zPlane::MAX_POLE_RADIUS).epsilon(1e-6));
    CHECK(Near(plane.pole(0), cplx(0.6, 0.8) * zPlane::MAX_POLE_RADIUS, 1e-5));
    REQUIRE(plane.moveZero(0, 0.0, 2.0));
    CHECK(Near(plane.zero(0), cplx(0.0, 2.0), 1e-5));

    // A section with a0 = 0 gets gain 1 when a zero moves; its zeroes are
    // then read back in their order, the larger first.
    plane.order(4);
    plane.coefficients(1, {0.f, 0.5f, 0.f, 0.f, 0.f});
    CHECK_FALSE(plane.finite(1));
    CHECK(plane.zero(2) == cplx());
    REQUIRE(plane.moveZero(3, 0.5, 0.0));
    CHECK(plane.finite(1));
    CHECK(plane.gain(1) == 1.f);
    CHECK(Near(plane.zero(2), 0.5, 1e-6));
    CHECK(Near(plane.zero(3), 0.0, 1e-6));

    // Invalid moves change nothing.
    const zPlane before = plane;
    CHECK_FALSE(plane.movePole(4, 0.1, 0.1));
    CHECK_FALSE(plane.moveZero(0, NAN, 0.0));
    CHECK_FALSE(plane.movePole(0, 0.0, INFINITY));
    for (UInt s = 0; s < 2; ++s) {
      CHECK(plane.coefficients(s).a1 == before.coefficients(s).a1);
      CHECK(plane.coefficients(s).b2 == before.coefficients(s).b2);
    }
  }

  TEST_CASE("zPlane: sections, order, cascades and the root lists (#1103)") {
    zPlane plane;
    CHECK(plane.sections() == 1);
    CHECK(plane.order() == 2);
    CHECK(plane.coefficients(0).a0 == 1.f);
    CHECK(plane.zero(0) == cplx());
    CHECK(plane.pole(1) == cplx());
    CHECK(plane.zero(2) == cplx());

    plane.order(5);
    CHECK(plane.sections() == 3);
    plane.order(0);
    CHECK(plane.order() == 2);
    plane.order(1000);
    CHECK(plane.sections() == zPlane::MAX_SECTIONS);
    plane.sections(0);
    CHECK(plane.sections() == 1);

    const float list[] = {2.f, 0.f, -0.5f, 0.f, 0.25f, 1.f, 0.5f, 0.f, -0.5f, 0.f, 9.f};
    REQUIRE(plane.cascade(list, 11) == 2);
    CHECK(plane.sections() == 2);
    float out[zPlane::MAX_SECTIONS * 5];
    REQUIRE(plane.coefficients(out, zPlane::MAX_SECTIONS * 5) == 10);
    for (int i = 0; i < 10; ++i)
      CHECK(out[i] == list[i]);
    // Whole sections only.
    CHECK(plane.coefficients(out, 7) == 5);
    REQUIRE(plane.gains(out, 10) == 2);
    CHECK(out[0] == 2.f);
    CHECK(out[1] == 1.f);

    // Zeroes of 2 - 0.5 z^-2: +-0.5; poles of 1 + 0.25 z^-2: +-0.5i.
    REQUIRE(plane.zeroes(out, 100) == 8);
    CHECK(out[0] == doctest::Approx(0.5));
    CHECK(out[1] == 0.f);
    CHECK(out[2] == doctest::Approx(-0.5));
    // Section 1: zeroes of 1 + 0.5 z^-1: 0 and -0.5.
    CHECK(out[4] == 0.f);
    CHECK(out[6] == doctest::Approx(-0.5));
    REQUIRE(plane.poles(out, 100) == 8);
    CHECK(out[0] == doctest::Approx(0.0).scale(1.0));
    CHECK(out[1] == doctest::Approx(0.5));
    CHECK(out[3] == doctest::Approx(-0.5));
    // Section 1: poles of 1 - 0.5 z^-1: 0.5 and 0.
    CHECK(out[4] == doctest::Approx(0.5));
    CHECK(out[6] == 0.f);
    CHECK(plane.zeroes(out, 7) == 4);
    CHECK(plane.zeroes(nullptr, 8) == 0);

    // Shrinking keeps the sections past the count.
    plane.sections(1);
    plane.sections(2);
    CHECK(plane.coefficients(1).a1 == 0.5f);

    // Invalid input changes nothing.
    const float bad[] = {1.f, 0.f, NAN, 0.f, 0.f};
    CHECK(plane.cascade(bad, 5) == 0);
    CHECK(plane.cascade(list, 4) == 0);
    CHECK(plane.cascade(nullptr, 5) == 0);
    plane.coefficient(0, 1, NAN);
    plane.coefficient(0, 5, 3.f);
    plane.coefficient(zPlane::MAX_SECTIONS, 0, 3.f);
    plane.coefficients(0, {1.f, INFINITY, 0.f, 0.f, 0.f});
    CHECK(plane.sections() == 2);
    CHECK(plane.coefficients(0).a0 == 2.f);
    CHECK(plane.coefficients(0).a1 == 0.f);
    CHECK(plane.coefficients(zPlane::MAX_SECTIONS).a0 == 1.f);
    plane.coefficient(0, 4, 0.5f);
    CHECK(plane.coefficients(0).b2 == 0.5f);

    // A long cascade sets MAX_SECTIONS.
    std::vector<float> many(5 * (zPlane::MAX_SECTIONS + 2), 0.f);
    CHECK(plane.cascade(many.data(), static_cast<UInt>(many.size())) == zPlane::MAX_SECTIONS);
  }

  TEST_CASE("zPlane: conversions, edits and lists allocate nothing (#1103)") {
    zPlane plane;
    float out[zPlane::MAX_SECTIONS * 5];
    const float list[] = {1.f, 0.2f, 0.3f, -0.4f, 0.5f, 0.5f, 0.f, 0.f, 0.f, 0.f};
    {
      TestHelpers::ProbeScope probe;
      CHECK(plane.cascade(list, 10) == 2);
      plane.constrainPoles(true).order(6);
      CHECK(plane.movePole(2, 0.3, 0.4));
      CHECK(plane.moveZero(5, -1.0, 0.0));
      CHECK(plane.zeroes(out, zPlane::MAX_SECTIONS * 5) == 12);
      CHECK(plane.poles(out, zPlane::MAX_SECTIONS * 5) == 12);
      CHECK(plane.gains(out, zPlane::MAX_SECTIONS * 5) == 3);
      CHECK(plane.coefficients(out, zPlane::MAX_SECTIONS * 5) == 15);
      const zPlane copy = plane;
      CHECK(copy.finite(0));
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
