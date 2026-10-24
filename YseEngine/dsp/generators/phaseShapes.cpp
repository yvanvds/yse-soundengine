#include "phaseShapes.hpp"

#include <cmath>

namespace {
  using YSE::DSP::buffer;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  inline UInt Shorter(UInt a, UInt b) {
    return a < b ? a : b;
  }

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  // The fractional part, in [0, 1). A tiny negative phase rounds x - floor(x)
  // up to exactly 1, which is the start of the next cycle.
  inline Flt Wrap(Flt x) {
    if (!std::isfinite(x)) return 0.f;
    const Flt w = x - std::floor(x);
    return w < 1.f ? w : 0.f;
  }

  inline Flt Clip(Flt x) {
    if (!std::isfinite(x)) return 0.f;
    return x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
  }

  inline Flt Point(Flt value, Flt fallback) {
    if (!std::isfinite(value)) return fallback;
    return value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
  }

  // u in 0..1 into lo..hi without forming hi - lo, which can overflow. Only
  // the static process() can pass a range that is not finite; it gives 0.
  inline Flt Scale(Flt u, Flt lo, Flt hi) {
    const Flt y = lo * (1.f - u) + hi * u;
    return std::isfinite(y) ? y : 0.f;
  }

  inline Flt Triangle(Flt phase, Flt peak, Flt lo, Flt hi) {
    const Flt x = Wrap(phase);
    const Flt p = Point(peak, 0.5f);
    // x < p implies p > 0; otherwise x >= p, and x < 1 implies p < 1.
    const Flt u = x < p ? x / p : (1.f - x) / (1.f - p);
    return Scale(u, lo, hi);
  }

  inline Flt Trapezoid(Flt phase, Flt up, Flt down, Flt lo, Flt hi, bool wrap) {
    const Flt x = wrap ? Wrap(phase) : Clip(phase);
    const Flt a = Point(up, YSE::DSP::trapezoidShape::DEFAULT_UP);
    Flt d = Point(down, YSE::DSP::trapezoidShape::DEFAULT_DOWN);
    if (d < a) d = a;
    Flt u = 1.f;
    if (x < a) {
      u = x / a;
    } else if (x >= d && d < 1.f) {
      u = (1.f - x) / (1.f - d);
    }
    return Scale(u, lo, hi);
  }

  inline void SetRange(Flt lo, Flt hi, Flt& low, Flt& high) {
    if (std::isfinite(lo)) low = lo;
    if (std::isfinite(hi)) high = hi;
  }
} // namespace

namespace YSE {
  namespace DSP {

    // ─── triangleShape ───────────────────────────────────────────────────────

    triangleShape::triangleShape() : output(STANDARD_BUFFERSIZE) {}

    Flt triangleShape::process(Flt phase, Flt peak, Flt lo, Flt hi) {
      return Triangle(phase, peak, lo, hi);
    }

    triangleShape& triangleShape::range(Flt lo, Flt hi) {
      SetRange(lo, hi, low, high);
      return *this;
    }

    buffer& triangleShape::operator()(buffer& phase, Flt peak) {
      const UInt length = Clamp(phase.getLength());
      Fit(output, length);
      const Flt* in = phase.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Triangle(in[i], peak, low, high);
      return output;
    }

    buffer& triangleShape::operator()(buffer& phase, buffer& peak) {
      const UInt length = Clamp(Shorter(phase.getLength(), peak.getLength()));
      Fit(output, length);
      const Flt* in = phase.getPtr();
      const Flt* p = peak.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Triangle(in[i], p[i], low, high);
      return output;
    }

    // ─── trapezoidShape ──────────────────────────────────────────────────────

    trapezoidShape::trapezoidShape() : output(STANDARD_BUFFERSIZE) {}

    Flt trapezoidShape::process(Flt phase, Flt up, Flt down, Flt lo, Flt hi, bool wrap) {
      return Trapezoid(phase, up, down, lo, hi, wrap);
    }

    trapezoidShape& trapezoidShape::range(Flt lo, Flt hi) {
      SetRange(lo, hi, low, high);
      return *this;
    }

    trapezoidShape& trapezoidShape::wrap(bool on) {
      wrapping = on;
      return *this;
    }

    buffer& trapezoidShape::operator()(buffer& phase, Flt up, Flt down) {
      const UInt length = Clamp(phase.getLength());
      Fit(output, length);
      const Flt* in = phase.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Trapezoid(in[i], up, down, low, high, wrapping);
      return output;
    }

    buffer& trapezoidShape::operator()(buffer& phase, buffer& up, buffer& down) {
      const UInt length =
          Clamp(Shorter(phase.getLength(), Shorter(up.getLength(), down.getLength())));
      Fit(output, length);
      const Flt* in = phase.getPtr();
      const Flt* a = up.getPtr();
      const Flt* d = down.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i)
        out[i] = Trapezoid(in[i], a[i], d[i], low, high, wrapping);
      return output;
    }

  } // namespace DSP
} // namespace YSE
