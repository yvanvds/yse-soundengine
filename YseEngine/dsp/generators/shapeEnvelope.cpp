#include "shapeEnvelope.hpp"

#include <cmath>
#include <utility>

namespace {
  using YSE::DSP::buffer;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  inline UInt Capacity(UInt requested) {
    if (requested < 1) return 1;
    if (requested > YSE::DSP::shapeEnvelope::MAX_POINTS) return YSE::DSP::shapeEnvelope::MAX_POINTS;
    return requested;
  }

  inline Flt Clamp01(Flt value) {
    if (value < 0.f) return 0.f;
    if (value > 1.f) return 1.f;
    return value;
  }

  // The exponent for curvature `c`, as `.function` bends its segments:
  // (1 + c) / (1 - c), infinite at +1 (hold until the end), 0 at -1 (jump
  // at the start).
  inline Flt Exponent(Flt c) {
    if (c >= 1.f) return INFINITY;
    if (c <= -1.f) return 0.f;
    return (1.f + c) / (1.f - c);
  }

  inline Flt Bend(Flt f, Flt exponent) {
    if (exponent == 1.f) return f;
    if (std::isinf(exponent)) return f >= 1.f ? 1.f : 0.f;
    if (exponent == 0.f) return f > 0.f ? 1.f : 0.f;
    return std::pow(f, exponent);
  }
} // namespace

namespace YSE {
  namespace DSP {

    shapeEnvelope::shapeEnvelope(UInt capacity) : output(STANDARD_BUFFERSIZE) {
      const UInt size = Capacity(capacity);
      for (store* s : {&current, &next}) {
        s->values.assign(size, 0.f);
        s->times.assign(size, 0.f);
        s->curves.assign(size, 0.f);
        s->exponents.assign(size, 1.f);
        s->ends.assign(size, 0.f);
      }
    }

    Flt shapeEnvelope::value(UInt point) const {
      return point < current.count ? current.values[point] : 0.f;
    }

    Flt shapeEnvelope::time(UInt point) const {
      return point < current.count ? current.times[point] : 0.f;
    }

    Flt shapeEnvelope::curve(UInt point) const {
      return point < current.count ? current.curves[point] : 0.f;
    }

    bool shapeEnvelope::breakpoints(Flt initial, const Flt* values, const Flt* times,
                                    const Flt* curves, UInt count) {
      if (count > capacity()) return false;
      if (count > 0 && (values == nullptr || times == nullptr)) return false;
      if (!std::isfinite(initial)) return false;
      for (UInt i = 0; i < count; ++i) {
        if (!std::isfinite(values[i]) || !std::isfinite(times[i])) return false;
        if (curves != nullptr && !std::isfinite(curves[i])) return false;
      }

      next.initial = initial;
      next.count = count;
      Flt end = 0.f;
      for (UInt i = 0; i < count; ++i) {
        const Flt t = times[i] > 0.f ? times[i] : 0.f;
        Flt c = curves != nullptr ? curves[i] : 0.f;
        if (c < -1.f) c = -1.f;
        if (c > 1.f) c = 1.f;
        next.values[i] = values[i];
        next.times[i] = t;
        next.curves[i] = c;
        next.exponents[i] = Exponent(c);
        end += t;
        next.ends[i] = end;
      }

      if (sync) {
        waiting = true;
      } else {
        Commit();
      }
      return true;
    }

    void shapeEnvelope::Commit() {
      // Swapping vectors exchanges their storage: no allocation.
      std::swap(current, next);
      waiting = false;
      segment = 0;
    }

    shapeEnvelope& shapeEnvelope::constant(UInt segments) {
      constantSegments = segments;
      return *this;
    }

    shapeEnvelope& shapeEnvelope::curved(bool on) {
      useCurves = on;
      return *this;
    }

    shapeEnvelope& shapeEnvelope::syncUpdate(bool on) {
      sync = on;
      if (!sync && waiting) Commit();
      return *this;
    }

    Flt shapeEnvelope::At(Flt ms) {
      const UInt n = current.count;
      if (n == 0) return current.initial;
      const Flt* ends = current.ends.data();

      // The first segment ending after `ms`, walked from the last one used.
      UInt k = segment < n ? segment : n - 1;
      while (k > 0 && ends[k - 1] > ms)
        k--;
      while (k < n && ends[k] <= ms)
        k++;
      segment = k;
      if (k >= n) return current.values[n - 1];

      const Flt start = k > 0 ? ends[k - 1] : 0.f;
      const Flt from = k > 0 ? current.values[k - 1] : current.initial;
      const Flt to = current.values[k];
      // ends[k] > ms >= start, so the segment has a length.
      Flt f = Clamp01((ms - start) / current.times[k]);
      if (useCurves) f = Bend(f, current.exponents[k]);
      return from + (to - from) * f;
    }

    buffer& shapeEnvelope::operator()(buffer& input) {
      const UInt length = Clamp(input.getLength());
      Fit(output, length);
      // The live rate, once per block (#634).
      const Flt msPerSample = 1000.f / static_cast<Flt>(SAMPLERATE);
      const Flt* in = input.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        Flt x = in[i];
        if (!std::isfinite(x)) x = 0.f;
        const Flt jump = x - previous;
        const bool start =
            std::fabs(jump) > RESET_THRESHOLD || (previous == 0.f && x != 0.f && !justStarted);
        if (start) {
          // A falling ramp starts by jumping up to near 1.
          rising = jump <= RESET_THRESHOLD;
          if (waiting) Commit();
          elapsed = 0;
          scaling = false;
          justStarted = true;
        } else {
          if (jump > 0.f) {
            rising = true;
          } else if (jump < 0.f) {
            rising = false;
          }
          justStarted = false;
          if (x == 0.f && previous == 0.f) {
            // Off: the clock waits at the start of the envelope.
            elapsed = 0;
            scaling = false;
          } else if (elapsed < 0xFFFFFFFFu) {
            elapsed++;
          }
        }
        previous = x;

        const Flt position = Clamp01(rising ? x : 1.f - x);
        const UInt n = current.count;
        const Flt total = n > 0 ? current.ends[n - 1] : 0.f;
        const UInt fixed = rising ? (constantSegments < n ? constantSegments : n) : 0;
        if (fixed == 0) {
          out[i] = At(position * total);
          continue;
        }

        const Flt fixedEnd = current.ends[fixed - 1];
        if (!scaling) {
          const Flt ms = static_cast<Flt>(elapsed) * msPerSample;
          if (ms < fixedEnd) {
            out[i] = At(ms);
            continue;
          }
          scaling = true;
          scaleFrom = position;
        }
        const Flt span = 1.f - scaleFrom;
        const Flt f = span > 0.f ? Clamp01((position - scaleFrom) / span) : 1.f;
        out[i] = At(fixedEnd + f * (total - fixedEnd));
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
