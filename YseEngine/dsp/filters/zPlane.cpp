#include "zPlane.hpp"
#include "filterDesign.hpp"

#include <algorithm>
#include <cmath>

namespace {
  using cplx = std::complex<double>;

  bool Finite(const YSE::DSP::biQuad::coefficientSet& c) {
    return std::isfinite(c.a0) && std::isfinite(c.a1) && std::isfinite(c.a2) &&
           std::isfinite(c.b1) && std::isfinite(c.b2);
  }

  // Writes the roots of the first `sections` sections as real/imaginary
  // pairs; `zeroes` picks which.
  UInt WriteRoots(const YSE::DSP::zPlane& plane, bool zeroes, Flt* out, UInt capacity) {
    if (out == nullptr) return 0;
    const UInt n = std::min(plane.sections(), capacity / 4);
    for (UInt i = 0; i < 2 * n; ++i) {
      const cplx r = zeroes ? plane.zero(i) : plane.pole(i);
      out[2 * i] = static_cast<Flt>(r.real());
      out[2 * i + 1] = static_cast<Flt>(r.imag());
    }
    return 4 * n;
  }
} // namespace

namespace YSE {
  namespace DSP {

    zPlane& zPlane::sections(UInt value) {
      count = std::clamp<UInt>(value, 1, MAX_SECTIONS);
      return *this;
    }

    zPlane& zPlane::order(UInt poles) {
      return sections((poles + 1) / 2);
    }

    zPlane& zPlane::coefficients(UInt section, const coefficientSet& set) {
      if (section < MAX_SECTIONS && Finite(set)) slots[section] = set;
      return *this;
    }

    zPlane& zPlane::coefficient(UInt section, UInt index, Flt value) {
      if (section >= MAX_SECTIONS || index > 4 || !std::isfinite(value)) return *this;
      coefficientSet& c = slots[section];
      Flt* fields[5] = {&c.a0, &c.a1, &c.a2, &c.b1, &c.b2};
      *fields[index] = value;
      return *this;
    }

    UInt zPlane::cascade(const Flt* values, UInt valueCount) {
      if (values == nullptr) return 0;
      const UInt n = std::min(valueCount / 5, MAX_SECTIONS);
      if (n == 0) return 0;
      for (UInt i = 0; i < 5 * n; ++i)
        if (!std::isfinite(values[i])) return 0;
      for (UInt s = 0; s < n; ++s) {
        const Flt* v = values + 5 * s;
        slots[s] = coefficientSet{v[0], v[1], v[2], v[3], v[4]};
      }
      count = n;
      return n;
    }

    zPlane::coefficientSet zPlane::coefficients(UInt section) const {
      if (section >= MAX_SECTIONS) return coefficientSet{1.f, 0.f, 0.f, 0.f, 0.f};
      return slots[section];
    }

    UInt zPlane::coefficients(Flt* out, UInt capacity) const {
      if (out == nullptr) return 0;
      const UInt n = std::min(count, capacity / 5);
      for (UInt s = 0; s < n; ++s) {
        const coefficientSet& c = slots[s];
        Flt* o = out + 5 * s;
        o[0] = c.a0;
        o[1] = c.a1;
        o[2] = c.a2;
        o[3] = c.b1;
        o[4] = c.b2;
      }
      return 5 * n;
    }

    Bool zPlane::finite(UInt section) const {
      return section < MAX_SECTIONS && slots[section].a0 != 0.f;
    }

    std::complex<Dbl> zPlane::zero(UInt index) const {
      if (index >= order()) return {};
      cplx z[2];
      cplx p[2];
      filterDesign::sectionRoots(slots[index / 2], z[0], z[1], p[0], p[1]);
      return z[index % 2];
    }

    std::complex<Dbl> zPlane::pole(UInt index) const {
      if (index >= order()) return {};
      cplx z[2];
      cplx p[2];
      filterDesign::sectionRoots(slots[index / 2], z[0], z[1], p[0], p[1]);
      return p[index % 2];
    }

    Flt zPlane::gain(UInt section) const {
      return section < MAX_SECTIONS ? slots[section].a0 : 1.f;
    }

    UInt zPlane::zeroes(Flt* out, UInt capacity) const {
      return WriteRoots(*this, true, out, capacity);
    }

    UInt zPlane::poles(Flt* out, UInt capacity) const {
      return WriteRoots(*this, false, out, capacity);
    }

    UInt zPlane::gains(Flt* out, UInt capacity) const {
      if (out == nullptr) return 0;
      const UInt n = std::min(count, capacity);
      for (UInt s = 0; s < n; ++s)
        out[s] = slots[s].a0;
      return n;
    }

    void zPlane::Move(cplx roots[2], UInt which, cplx to) {
      const UInt other = 1 - which;
      const bool pair = roots[which].imag() != 0.0;
      roots[which] = to;
      if (to.imag() != 0.0 || pair) roots[other] = std::conj(to);
    }

    Bool zPlane::moveZero(UInt index, Dbl real, Dbl imaginary) {
      if (index >= order() || !std::isfinite(real) || !std::isfinite(imaginary)) return false;
      coefficientSet& c = slots[index / 2];
      cplx z[2];
      cplx p[2];
      const bool hasZeroes = filterDesign::sectionRoots(c, z[0], z[1], p[0], p[1]);
      Move(z, index % 2, cplx(real, imaginary));
      const coefficientSet next =
          filterDesign::sectionFromRoots(hasZeroes ? c.a0 : 1.0, z[0], z[1], p[0], p[1]);
      if (!Finite(next)) return false;
      // The poles stay exactly as they were.
      c.a0 = next.a0;
      c.a1 = next.a1;
      c.a2 = next.a2;
      return true;
    }

    Bool zPlane::movePole(UInt index, Dbl real, Dbl imaginary) {
      if (index >= order() || !std::isfinite(real) || !std::isfinite(imaginary)) return false;
      cplx to(real, imaginary);
      if (constrain && std::abs(to) > MAX_POLE_RADIUS) to *= MAX_POLE_RADIUS / std::abs(to);
      coefficientSet& c = slots[index / 2];
      cplx z[2];
      cplx p[2];
      filterDesign::sectionRoots(c, z[0], z[1], p[0], p[1]);
      Move(p, index % 2, to);
      const coefficientSet next = filterDesign::sectionFromRoots(1.0, z[0], z[1], p[0], p[1]);
      if (!Finite(next)) return false;
      // The zeroes and gain stay exactly as they were.
      c.b1 = next.b1;
      c.b2 = next.b2;
      return true;
    }

  } // namespace DSP
} // namespace YSE
