#include "filterResponse.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace {
  using cplx = std::complex<double>;

  constexpr double kPi = 3.14159265358979323846;
  constexpr double kTwoPi = 2.0 * kPi;

  // The value and the group delay of c0 + c1 z^-1 + c2 z^-2 on the unit
  // circle, at z = e^(jw).
  struct Evaluated {
    cplx value;
    double delay;
  };

  Evaluated Evaluate(const double* c, cplx e1, cplx e2) {
    const cplx s = c[0] + c[1] * e1 + c[2] * e2;
    const cplx d = c[1] * e1 + 2.0 * c[2] * e2;
    const double size = std::fabs(c[0]) + std::fabs(c[1]) + std::fabs(c[2]);
    const double power = std::norm(s);
    double delay = 0.0;
    if (power > 1e-20 * size * size) {
      delay = (d * std::conj(s)).real() / power;
    } else {
      // Exactly on a zero: half the degree, the group delay of a polynomial
      // whose zeroes lie on the unit circle.
      delay = c[2] != 0.0 ? 1.0 : (c[1] != 0.0 ? 0.5 : 0.0);
    }
    return {s, delay};
  }

  template <typename T> bool AllFinite(const T* values, unsigned int count) {
    for (unsigned int i = 0; i < count; ++i)
      if (!std::isfinite(values[i])) return false;
    return true;
  }
} // namespace

namespace YSE {
  namespace DSP {

    filterResponse::filterResponse(UInt capacity)
      : coefficientsIn(static_cast<std::size_t>(MAX_SECTIONS) * 5, 0.0),
        used(std::clamp<UInt>(capacity, 1, MAX_POINTS)),
        magnitudes(used, 0.f),
        phases(used, 0.f),
        phaseDelays(used, 0.f),
        groupDelays(used, 0.f),
        impulses(used, 0.f),
        steps(used, 0.f) {}

    filterResponse& filterResponse::points(UInt count) {
      used = std::clamp<UInt>(count, 1, capacity());
      return *this;
    }

    Dbl filterResponse::frequency(UInt index) const {
      return kPi * static_cast<double>(index) / static_cast<double>(used);
    }

    Bool filterResponse::analyse(const Flt* values, UInt count) {
      if (values == nullptr) return false;
      const UInt n = std::min(count, MAX_SECTIONS * 5) / 5 * 5;
      if (n == 0 || !AllFinite(values, n)) return false;
      for (UInt i = 0; i < n; ++i)
        coefficientsIn[i] = static_cast<double>(values[i]);
      return Analyse(n);
    }

    Bool filterResponse::analyse(const Dbl* values, UInt count) {
      if (values == nullptr) return false;
      const UInt n = std::min(count, MAX_SECTIONS * 5) / 5 * 5;
      if (n == 0 || !AllFinite(values, n)) return false;
      std::copy(values, values + n, coefficientsIn.begin());
      return Analyse(n);
    }

    Bool filterResponse::analyse(const filterDesign& design) {
      Dbl values[filterDesign::MAX_SECTIONS * 5];
      const UInt n = design.coefficients(values, filterDesign::MAX_SECTIONS * 5);
      return analyse(values, n);
    }

    Bool filterResponse::Analyse(UInt count) {
      sectionCount = count / 5;
      const double* c = coefficientsIn.data();

      // The frequency response.
      double previous = 0.0;
      for (UInt i = 0; i < used; ++i) {
        const double w = frequency(i);
        const cplx e1 = std::polar(1.0, -w);
        const cplx e2 = std::polar(1.0, -2.0 * w);
        cplx h(1.0, 0.0);
        double angle = 0.0;
        double delay = 0.0;
        for (UInt s = 0; s < sectionCount; ++s) {
          const double* section = c + 5 * s;
          const double denominator[3] = {1.0, section[3], section[4]};
          const Evaluated top = Evaluate(section, e1, e2);
          const Evaluated bottom = Evaluate(denominator, e1, e2);
          h *= top.value / bottom.value;
          angle += std::arg(top.value) - std::arg(bottom.value);
          delay += top.delay - bottom.delay;
        }
        // Unwrap: no step of more than pi between neighbouring points, and
        // the first point within -pi .. pi.
        if (i == 0) {
          angle = std::remainder(angle, kTwoPi);
        } else {
          angle -= kTwoPi * std::round((angle - previous) / kTwoPi);
        }
        previous = angle;
        magnitudes[i] = static_cast<Flt>(std::abs(h));
        phases[i] = static_cast<Flt>(angle);
        groupDelays[i] = static_cast<Flt>(delay);
        phaseDelays[i] = static_cast<Flt>(i == 0 ? delay : -angle / w);
      }

      // The impulse and step responses: the cascade run on 1 0 0 ... and on
      // 1 1 1 ..., in double precision, from silence.
      const auto run = [&](std::vector<Flt>& out, bool held) {
        double x1[MAX_SECTIONS] = {};
        double x2[MAX_SECTIONS] = {};
        double y1[MAX_SECTIONS] = {};
        double y2[MAX_SECTIONS] = {};
        for (UInt n = 0; n < used; ++n) {
          double x = (n == 0 || held) ? 1.0 : 0.0;
          for (UInt s = 0; s < sectionCount; ++s) {
            const double* k = c + 5 * s;
            const double y = k[0] * x + k[1] * x1[s] + k[2] * x2[s] - k[3] * y1[s] - k[4] * y2[s];
            x2[s] = x1[s];
            x1[s] = x;
            y2[s] = y1[s];
            y1[s] = y;
            x = y;
          }
          out[n] = static_cast<Flt>(x);
        }
      };
      run(impulses, false);
      run(steps, true);
      return true;
    }

  } // namespace DSP
} // namespace YSE
