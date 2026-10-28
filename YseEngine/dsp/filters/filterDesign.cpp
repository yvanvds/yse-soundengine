#include "filterDesign.hpp"

#include <algorithm>
#include <cmath>

namespace {
  using cplx = std::complex<double>;

  constexpr double kPi = 3.14159265358979323846;

  // The bilinear transform runs at a sample rate of 2, so a frequency
  // normalized to the Nyquist frequency (0 .. 1) prewarps to
  // 2 * fs * tan(pi * w / fs) = 4 * tan(pi * w / 2).
  constexpr double kFs = 2.0;

  // A set of roots on the stack. Twice the order: a bandpass of the highest
  // order doubles its prototype, and the arrays never need more than that.
  struct Roots {
    std::array<cplx, 2 * YSE::DSP::filterDesign::MAX_ORDER> v{};
    unsigned int n = 0;
    void Push(cplx c) {
      if (n < v.size()) v[n++] = c;
    }
  };

  cplx Product(const Roots& r, cplx offset, double sign) {
    // prod(offset + sign * r)
    cplx result(1.0, 0.0);
    for (unsigned int i = 0; i < r.n; ++i)
      result *= offset + sign * r.v[i];
    return result;
  }

  bool IsReal(cplx c) {
    return std::fabs(c.imag()) <= 1e-9 * std::max(1.0, std::abs(c));
  }

  // How far a root is from the unit circle.
  double Distance(cplx c) {
    return std::fabs(1.0 - std::abs(c));
  }

  double Warp(double normalized) {
    return 2.0 * kFs * std::tan(kPi * normalized / kFs);
  }

  // Each root r becomes r + sqrt(r^2 - wo^2) and r - sqrt(r^2 - wo^2): the
  // lowpass-to-bandpass substitution, after scaling.
  void Split(Roots& r, double wo) {
    Roots out;
    for (unsigned int i = 0; i < r.n; ++i)
      out.Push(r.v[i] + std::sqrt(r.v[i] * r.v[i] - wo * wo));
    for (unsigned int i = 0; i < r.n; ++i)
      out.Push(r.v[i] - std::sqrt(r.v[i] * r.v[i] - wo * wo));
    r = out;
  }
} // namespace

namespace YSE {
  namespace DSP {

    filterDesign& filterDesign::order(UInt value) {
      requestedOrder = std::clamp<UInt>(value, 1, MAX_ORDER);
      return *this;
    }

    filterDesign& filterDesign::frequency(Flt value) {
      if (std::isfinite(value)) frequencies[0] = value;
      return *this;
    }

    filterDesign& filterDesign::frequency(Flt first, Flt second) {
      if (std::isfinite(first)) frequencies[0] = first;
      if (std::isfinite(second)) frequencies[1] = second;
      return *this;
    }

    filterDesign& filterDesign::sampleRate(Flt value) {
      if (std::isfinite(value) && value >= 0.f) rate = value;
      return *this;
    }

    filterDesign& filterDesign::passbandRipple(Flt decibels) {
      if (std::isfinite(decibels) && decibels > 0.f) ripple = decibels;
      return *this;
    }

    filterDesign& filterDesign::stopbandAttenuation(Flt decibels) {
      if (std::isfinite(decibels) && decibels > 0.f) attenuation = decibels;
      return *this;
    }

    void filterDesign::Fail() {
      rootCount = 0;
      sectionCount = 0;
      gainZ = 0.0;
      zeroes.fill(cplx());
      polesZ.fill(cplx());
      sos.fill(0.0);
      numeratorTF.fill(0.0);
      denominatorTF.fill(0.0);
    }

    Bool filterDesign::design() {
      const bool band = shape == RESPONSE::BANDPASS || shape == RESPONSE::BANDSTOP;
      const unsigned int n = band ? (requestedOrder + 1) / 2 : requestedOrder;

      // The frequencies, normalized to the Nyquist frequency.
      const double sampleRateNow =
          rate > 0.f ? static_cast<double>(rate) : static_cast<double>(SAMPLERATE);
      const auto normalize = [&](Flt f) {
        switch (unit) {
        case UNITS::NORMALIZED:
          return static_cast<double>(f);
        case UNITS::RADIANS:
          return static_cast<double>(f) / kPi;
        case UNITS::HERTZ:
        default:
          return sampleRateNow > 0.0 ? static_cast<double>(f) / (sampleRateNow * 0.5) : -1.0;
        }
      };
      const auto inside = [](double w) { return w > 0.0 && w < 1.0; };
      double w0 = normalize(frequencies[0]);
      double w1 = normalize(frequencies[1]);
      if (!inside(w0) || (band && !inside(w1))) {
        Fail();
        return false;
      }
      if (band) {
        if (w0 > w1) std::swap(w0, w1);
        if (!(w0 < w1)) {
          Fail();
          return false;
        }
      }

      // The analog prototype, cutoff 1 rad/s.
      Roots z;
      Roots p;
      double k = 1.0;
      const auto nd = static_cast<double>(n);
      switch (family) {
      case TOPOLOGY::CHEBYSHEV_1: {
        const double eps = std::sqrt(std::pow(10.0, 0.1 * ripple) - 1.0);
        const double mu = std::asinh(1.0 / eps) / nd;
        for (unsigned int i = 0; i < n; ++i) {
          const double m = -nd + 1.0 + 2.0 * i;
          p.Push(-std::sinh(cplx(mu, kPi * m / (2.0 * nd))));
        }
        k = Product(p, 0.0, -1.0).real();
        if (n % 2 == 0) k /= std::sqrt(1.0 + eps * eps);
        break;
      }
      case TOPOLOGY::CHEBYSHEV_2: {
        const double de = 1.0 / std::sqrt(std::pow(10.0, 0.1 * attenuation) - 1.0);
        const double mu = std::asinh(1.0 / de) / nd;
        for (unsigned int i = 0; i < n; ++i) {
          const double m = -nd + 1.0 + 2.0 * i;
          // m = 0 (odd orders) is the zero at infinity.
          if (m != 0.0) z.Push(cplx(0.0, 1.0 / std::sin(m * kPi / (2.0 * nd))));
          const cplx b = -std::exp(cplx(0.0, kPi * m / (2.0 * nd)));
          p.Push(1.0 / cplx(std::sinh(mu) * b.real(), std::cosh(mu) * b.imag()));
        }
        k = (Product(p, 0.0, -1.0) / Product(z, 0.0, -1.0)).real();
        break;
      }
      case TOPOLOGY::BUTTERWORTH:
      default:
        for (unsigned int i = 0; i < n; ++i) {
          const double m = -nd + 1.0 + 2.0 * i;
          p.Push(-std::exp(cplx(0.0, kPi * m / (2.0 * nd))));
        }
        k = 1.0;
        break;
      }

      // The response, at prewarped frequencies.
      const unsigned int degree = p.n - z.n;
      switch (shape) {
      case RESPONSE::HIGHPASS: {
        const double wo = Warp(w0);
        k *= (Product(z, 0.0, -1.0) / Product(p, 0.0, -1.0)).real();
        for (unsigned int i = 0; i < z.n; ++i)
          z.v[i] = wo / z.v[i];
        for (unsigned int i = 0; i < p.n; ++i)
          p.v[i] = wo / p.v[i];
        for (unsigned int i = 0; i < degree; ++i)
          z.Push(0.0);
        break;
      }
      case RESPONSE::BANDPASS: {
        const double lo = Warp(w0);
        const double hi = Warp(w1);
        const double bw = hi - lo;
        const double wo = std::sqrt(lo * hi);
        for (unsigned int i = 0; i < z.n; ++i)
          z.v[i] *= bw / 2.0;
        for (unsigned int i = 0; i < p.n; ++i)
          p.v[i] *= bw / 2.0;
        Split(z, wo);
        Split(p, wo);
        for (unsigned int i = 0; i < degree; ++i)
          z.Push(0.0);
        k *= std::pow(bw, static_cast<double>(degree));
        break;
      }
      case RESPONSE::BANDSTOP: {
        const double lo = Warp(w0);
        const double hi = Warp(w1);
        const double bw = hi - lo;
        const double wo = std::sqrt(lo * hi);
        k *= (Product(z, 0.0, -1.0) / Product(p, 0.0, -1.0)).real();
        for (unsigned int i = 0; i < z.n; ++i)
          z.v[i] = (bw / 2.0) / z.v[i];
        for (unsigned int i = 0; i < p.n; ++i)
          p.v[i] = (bw / 2.0) / p.v[i];
        Split(z, wo);
        Split(p, wo);
        for (unsigned int i = 0; i < degree; ++i)
          z.Push(cplx(0.0, wo));
        for (unsigned int i = 0; i < degree; ++i)
          z.Push(cplx(0.0, -wo));
        break;
      }
      case RESPONSE::LOWPASS:
      default: {
        const double wo = Warp(w0);
        for (unsigned int i = 0; i < z.n; ++i)
          z.v[i] *= wo;
        for (unsigned int i = 0; i < p.n; ++i)
          p.v[i] *= wo;
        k *= std::pow(wo, static_cast<double>(degree));
        break;
      }
      }

      // The bilinear transform; zeroes at infinity land on -1.
      const double fs2 = 2.0 * kFs;
      const unsigned int remaining = p.n - z.n;
      k *= (Product(z, fs2, -1.0) / Product(p, fs2, -1.0)).real();
      for (unsigned int i = 0; i < z.n; ++i)
        z.v[i] = (fs2 + z.v[i]) / (fs2 - z.v[i]);
      for (unsigned int i = 0; i < p.n; ++i)
        p.v[i] = (fs2 + p.v[i]) / (fs2 - p.v[i]);
      for (unsigned int i = 0; i < remaining; ++i)
        z.Push(-1.0);

      if (p.n == 0 || p.n > MAX_ORDER || z.n != p.n || !std::isfinite(k)) {
        Fail();
        return false;
      }
      Fail();
      rootCount = p.n;
      for (unsigned int i = 0; i < p.n; ++i) {
        zeroes[i] = z.v[i];
        polesZ[i] = p.v[i];
      }
      gainZ = k;
      if (!BuildSections()) {
        Fail();
        return false;
      }
      BuildTransferFunction();
      return true;
    }

    Bool filterDesign::BuildSections() {
      // Real roots, and complex roots by their upper half (the lower half are
      // their conjugates).
      std::array<double, MAX_ORDER> realPoles{};
      std::array<cplx, MAX_ORDER> upperPoles{};
      std::array<double, MAX_ORDER> realZeroes{};
      std::array<cplx, MAX_ORDER> upperZeroes{};
      unsigned int nRealPoles = 0;
      unsigned int nUpperPoles = 0;
      unsigned int nRealZeroes = 0;
      unsigned int nUpperZeroes = 0;
      for (unsigned int i = 0; i < rootCount; ++i) {
        if (IsReal(polesZ[i]))
          realPoles[nRealPoles++] = polesZ[i].real();
        else if (polesZ[i].imag() > 0.0)
          upperPoles[nUpperPoles++] = polesZ[i];
        if (IsReal(zeroes[i]))
          realZeroes[nRealZeroes++] = zeroes[i].real();
        else if (zeroes[i].imag() > 0.0)
          upperZeroes[nUpperZeroes++] = zeroes[i];
      }
      // Every complex root needs its conjugate.
      if (nRealPoles + 2 * nUpperPoles != rootCount || nRealZeroes + 2 * nUpperZeroes != rootCount)
        return false;

      std::array<bool, MAX_ORDER> poleUsed{};
      std::array<bool, MAX_ORDER> zeroUsed{};
      std::array<bool, MAX_ORDER> upperZeroUsed{};

      // The nearest unused real zero to `to`, or -1.
      const auto nearestRealZero = [&](cplx to) {
        int best = -1;
        for (unsigned int i = 0; i < nRealZeroes; ++i) {
          if (zeroUsed[i]) continue;
          if (best < 0 || std::abs(realZeroes[i] - to) < std::abs(realZeroes[best] - to))
            best = static_cast<int>(i);
        }
        return best;
      };

      unsigned int written = 0;
      const auto write = [&](double a1, double a2, double b1, double b2) {
        double* s = sos.data() + 5 * written;
        s[0] = 1.0;
        s[1] = a1;
        s[2] = a2;
        s[3] = b1;
        s[4] = b2;
        ++written;
      };

      // An odd number of real poles leaves one for a first-order section: the
      // one farthest from the unit circle, with the nearest real zero (an odd
      // number of real poles means an odd number of real zeroes).
      if (nRealPoles % 2 == 1) {
        unsigned int far = 0;
        for (unsigned int i = 1; i < nRealPoles; ++i)
          if (Distance(realPoles[i]) > Distance(realPoles[far])) far = i;
        poleUsed[far] = true;
        const int zero = nearestRealZero(realPoles[far]);
        if (zero < 0) return false;
        zeroUsed[zero] = true;
        write(-realZeroes[zero], 0.0, -realPoles[far], 0.0);
      }

      // Second-order pole pairs: each complex pole with its conjugate, and
      // the remaining real poles two by two, the closest to the unit circle
      // together.
      struct Pair {
        cplx a; // the pole the zeroes are chosen for
        cplx b;
        double distance;
      };
      std::array<Pair, MAX_SECTIONS> pairs{};
      unsigned int nPairs = 0;
      for (unsigned int i = 0; i < nUpperPoles; ++i)
        pairs[nPairs++] = {upperPoles[i], std::conj(upperPoles[i]), Distance(upperPoles[i])};
      for (;;) {
        int first = -1;
        for (unsigned int i = 0; i < nRealPoles; ++i)
          if (!poleUsed[i] && (first < 0 || Distance(realPoles[i]) < Distance(realPoles[first])))
            first = static_cast<int>(i);
        if (first < 0) break;
        poleUsed[first] = true;
        int second = -1;
        for (unsigned int i = 0; i < nRealPoles; ++i)
          if (!poleUsed[i] && (second < 0 || Distance(realPoles[i]) < Distance(realPoles[second])))
            second = static_cast<int>(i);
        if (second < 0) return false;
        poleUsed[second] = true;
        pairs[nPairs++] = {realPoles[first], realPoles[second], Distance(realPoles[first])};
      }
      // Farthest from the unit circle first, the sharpest resonance last. A
      // stable insertion sort in place: std::stable_sort asks for a
      // temporary buffer (libstdc++ always does), which allocates.
      for (unsigned int i = 1; i < nPairs; ++i) {
        const Pair moving = pairs[i];
        unsigned int j = i;
        for (; j > 0 && pairs[j - 1].distance < moving.distance; --j)
          pairs[j] = pairs[j - 1];
        pairs[j] = moving;
      }

      // Zeroes, the closest pole pair choosing first.
      std::array<cplx, MAX_SECTIONS> zeroA{};
      std::array<cplx, MAX_SECTIONS> zeroB{};
      for (unsigned int j = nPairs; j-- > 0;) {
        const cplx to = pairs[j].a;
        int upper = -1;
        for (unsigned int i = 0; i < nUpperZeroes; ++i)
          if (!upperZeroUsed[i] &&
              (upper < 0 || std::abs(upperZeroes[i] - to) < std::abs(upperZeroes[upper] - to)))
            upper = static_cast<int>(i);
        const int real = nearestRealZero(to);
        if (upper >= 0 &&
            (real < 0 || std::abs(upperZeroes[upper] - to) <= std::abs(realZeroes[real] - to))) {
          upperZeroUsed[upper] = true;
          zeroA[j] = upperZeroes[upper];
          zeroB[j] = std::conj(upperZeroes[upper]);
        } else {
          if (real < 0) return false;
          zeroUsed[real] = true;
          const int other = nearestRealZero(to);
          if (other < 0) return false;
          zeroUsed[other] = true;
          zeroA[j] = realZeroes[real];
          zeroB[j] = realZeroes[other];
        }
      }
      for (unsigned int j = 0; j < nPairs; ++j) {
        write(-(zeroA[j] + zeroB[j]).real(), (zeroA[j] * zeroB[j]).real(),
              -(pairs[j].a + pairs[j].b).real(), (pairs[j].a * pairs[j].b).real());
      }
      sectionCount = written;

      // The overall gain goes in the first section.
      for (unsigned int i = 0; i < 3; ++i)
        sos[i] *= gainZ;
      return sectionCount > 0;
    }

    void filterDesign::BuildTransferFunction() {
      // prod(1 - r z^-1), expanded.
      const auto expand = [this](const std::array<cplx, MAX_ORDER>& roots,
                                 std::array<Dbl, MAX_ORDER + 1>& out, double scale) {
        std::array<cplx, MAX_ORDER + 1> c{};
        c[0] = 1.0;
        for (unsigned int r = 0; r < rootCount; ++r)
          for (unsigned int i = r + 1; i > 0; --i)
            c[i] -= roots[r] * c[i - 1];
        for (unsigned int i = 0; i <= rootCount; ++i)
          out[i] = scale * c[i].real();
      };
      expand(zeroes, numeratorTF, gainZ);
      expand(polesZ, denominatorTF, 1.0);
    }

    biQuad::coefficientSet filterDesign::section(UInt index) const {
      biQuad::coefficientSet set;
      if (index >= sectionCount) return set;
      const double* s = sos.data() + 5 * index;
      set.a0 = static_cast<Flt>(s[0]);
      set.a1 = static_cast<Flt>(s[1]);
      set.a2 = static_cast<Flt>(s[2]);
      set.b1 = static_cast<Flt>(s[3]);
      set.b2 = static_cast<Flt>(s[4]);
      return set;
    }

    UInt filterDesign::coefficients(Flt* out, UInt capacity) const {
      if (out == nullptr) return 0;
      const UInt count = std::min(sectionCount, capacity / 5) * 5;
      for (UInt i = 0; i < count; ++i)
        out[i] = static_cast<Flt>(sos[i]);
      return count;
    }

    namespace {
      // The roots of z^2 + b z + c: a complex pair upper half first, a real
      // pair the larger first, the larger computed without cancellation.
      void QuadraticRoots(double b, double c, cplx& first, cplx& second) {
        const double disc = b * b - 4.0 * c;
        if (disc < 0.0) {
          const double im = 0.5 * std::sqrt(-disc);
          first = cplx(-0.5 * b, im);
          second = cplx(-0.5 * b, -im);
          return;
        }
        const double q = -0.5 * (b + std::copysign(std::sqrt(disc), b));
        double r0 = q;
        double r1 = q != 0.0 ? c / q : 0.0;
        if (r1 > r0) std::swap(r0, r1);
        first = r0;
        second = r1;
      }
    } // namespace

    Bool filterDesign::sectionRoots(const biQuad::coefficientSet& set, cplx& zero0, cplx& zero1,
                                    cplx& pole0, cplx& pole1) {
      const double a0 = set.a0;
      const double a1 = set.a1;
      const double a2 = set.a2;
      const double b1 = set.b1;
      const double b2 = set.b2;
      zero0 = zero1 = pole0 = pole1 = cplx();
      if (!std::isfinite(a0) || !std::isfinite(a1) || !std::isfinite(a2) || !std::isfinite(b1) ||
          !std::isfinite(b2))
        return false;
      QuadraticRoots(b1, b2, pole0, pole1);
      if (a0 == 0.0) return false;
      QuadraticRoots(a1 / a0, a2 / a0, zero0, zero1);
      return true;
    }

    biQuad::coefficientSet filterDesign::sectionFromRoots(Dbl gain, cplx zero0, cplx zero1,
                                                          cplx pole0, cplx pole1) {
      biQuad::coefficientSet set;
      set.a0 = static_cast<Flt>(gain);
      set.a1 = static_cast<Flt>(-gain * (zero0 + zero1).real());
      set.a2 = static_cast<Flt>(gain * (zero0 * zero1).real());
      set.b1 = static_cast<Flt>(-(pole0 + pole1).real());
      set.b2 = static_cast<Flt>((pole0 * pole1).real());
      return set;
    }

    UInt filterDesign::coefficients(Dbl* out, UInt capacity) const {
      if (out == nullptr) return 0;
      const UInt count = std::min(sectionCount, capacity / 5) * 5;
      for (UInt i = 0; i < count; ++i)
        out[i] = sos[i];
      return count;
    }

  } // namespace DSP
} // namespace YSE
