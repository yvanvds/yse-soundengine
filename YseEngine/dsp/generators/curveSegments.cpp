#include "curveSegments.hpp"
#include "curveBend.hpp"

#include <cmath>

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
    if (requested > YSE::DSP::curveSegments::MAX_POINTS) return YSE::DSP::curveSegments::MAX_POINTS;
    return requested;
  }

  inline Flt CleanTime(Flt ms) {
    return ms > 0.f && std::isfinite(ms) ? ms : 0.f;
  }

  inline Flt CleanCurve(Flt c) {
    if (std::isnan(c)) return 0.f;
    if (c < -1.f) return -1.f;
    if (c > 1.f) return 1.f;
    return c;
  }

  // A time in ms as samples at the live rate.
  inline Dbl Samples(Flt ms) {
    if (YSE::SAMPLERATE <= 0) return 0.0;
    return static_cast<Dbl>(ms) * static_cast<Dbl>(YSE::SAMPLERATE) / 1000.0;
  }

  using YSE::DSP::curveBend::Bend;
  using YSE::DSP::curveBend::Exponent;
} // namespace

namespace YSE {
  namespace DSP {

    curveSegments::curveSegments(UInt capacity) : output(STANDARD_BUFFERSIZE) {
      const UInt size = Capacity(capacity);
      targets.assign(size, 0.f);
      times.assign(size, 0.f);
      curves.assign(size, 0.f);
    }

    UInt curveSegments::segments(const Flt* newTargets, const Flt* newTimes, const Flt* newCurves,
                                 UInt length) {
      if (length == 0 || newTargets == nullptr || newTimes == nullptr) return 0;
      const UInt n = length > capacity() ? capacity() : length;
      for (UInt i = 0; i < n; ++i) {
        if (!std::isfinite(newTargets[i])) return 0;
      }
      for (UInt i = 0; i < n; ++i) {
        targets[i] = newTargets[i];
        times[i] = CleanTime(newTimes[i]);
        curves[i] = newCurves != nullptr ? CleanCurve(newCurves[i]) : 0.f;
      }
      count = n;
      index = 0;
      held = false;
      elapsed = 0.0;
      Begin();
      return n;
    }

    curveSegments& curveSegments::jump(Flt value) {
      if (!std::isfinite(value)) return *this;
      current = value;
      count = 0;
      index = 0;
      held = false;
      elapsed = 0.0;
      return *this;
    }

    curveSegments& curveSegments::stop() {
      count = 0;
      index = 0;
      held = false;
      elapsed = 0.0;
      return *this;
    }

    curveSegments& curveSegments::pause() {
      held = true;
      return *this;
    }

    curveSegments& curveSegments::resume() {
      held = false;
      return *this;
    }

    curveSegments& curveSegments::shape(SHAPE value) {
      mode = value == EXPONENTIAL ? EXPONENTIAL : POWER;
      if (index < count) {
        // The running segment keeps its start and its place.
        const Flt start = from;
        Begin();
        from = start;
      }
      return *this;
    }

    curveSegments& curveSegments::reset() {
      current = 0.f;
      from = 0.f;
      count = 0;
      index = 0;
      held = false;
      ended = false;
      elapsed = 0.0;
      return *this;
    }

    void curveSegments::Begin() {
      from = current;
      const Flt c = curves[index];
      if (mode == POWER) {
        bend = Exponent(c);
        scale = 1.f;
        return;
      }
      // k = 2 ln((1 + c) / (1 - c)): infinite at the limits, 0 when straight.
      if (c >= 1.f) {
        bend = INFINITY;
      } else if (c <= -1.f) {
        bend = -INFINITY;
      } else {
        bend = 2.f * std::log((1.f + c) / (1.f - c));
      }
      scale = (std::isinf(bend) || bend == 0.f) ? 1.f : 1.f / std::expm1(bend);
    }

    Flt curveSegments::Shape(Flt f) const {
      if (mode == POWER) return Bend(f, bend);
      if (bend == 0.f) return f;
      if (std::isinf(bend)) {
        if (bend > 0.f) return f >= 1.f ? 1.f : 0.f;
        return f > 0.f ? 1.f : 0.f;
      }
      return std::expm1(bend * f) * scale;
    }

    Flt curveSegments::Step() {
      if (index >= count || held) return current;
      elapsed += 1.0;
      for (;;) {
        // Read on every sample, so the segment follows the live rate.
        const Dbl length = Samples(times[index]);
        if (elapsed < length) {
          const Flt f = static_cast<Flt>(elapsed / length);
          current = from + (targets[index] - from) * Shape(f);
          return current;
        }
        // The segment is over; what is left of the sample belongs to the
        // next one. A segment of no length takes none of it.
        current = targets[index];
        elapsed -= length;
        ++index;
        if (index >= count) {
          count = 0;
          index = 0;
          elapsed = 0.0;
          ended = true;
          return current;
        }
        Begin();
      }
    }

    buffer& curveSegments::operator()(UInt length) {
      ended = false;
      const UInt n = Clamp(length);
      Fit(output, n);
      Flt* out = output.getPtr();
      for (UInt i = 0; i < n; ++i)
        out[i] = Step();
      return output;
    }

  } // namespace DSP
} // namespace YSE
