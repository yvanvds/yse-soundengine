#include "lineSegments.hpp"

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
    if (requested > YSE::DSP::lineSegments::MAX_POINTS) return YSE::DSP::lineSegments::MAX_POINTS;
    return requested;
  }

  inline Flt CleanTime(Flt ms) {
    return ms > 0.f && std::isfinite(ms) ? ms : 0.f;
  }

  // A time in ms as samples at the live rate.
  inline Dbl Samples(Flt ms) {
    if (YSE::SAMPLERATE <= 0) return 0.0;
    return static_cast<Dbl>(ms) * static_cast<Dbl>(YSE::SAMPLERATE) / 1000.0;
  }
} // namespace

namespace YSE {
  namespace DSP {

    lineSegments::lineSegments(UInt capacity)
      : output(STANDARD_BUFFERSIZE), busy(STANDARD_BUFFERSIZE) {
      const UInt size = Capacity(capacity);
      targets.assign(size, 0.f);
      times.assign(size, 0.f);
    }

    UInt lineSegments::segments(const Flt* newTargets, const Flt* newTimes, UInt length) {
      if (length == 0 || newTargets == nullptr || newTimes == nullptr) return 0;
      const UInt n = length > capacity() ? capacity() : length;
      for (UInt i = 0; i < n; ++i) {
        if (!std::isfinite(newTargets[i])) return 0;
      }
      for (UInt i = 0; i < n; ++i) {
        targets[i] = newTargets[i];
        times[i] = CleanTime(newTimes[i]);
      }
      count = n;
      index = 0;
      held = false;
      jumped = false;
      elapsed = 0.0;
      from = current;
      return n;
    }

    lineSegments& lineSegments::set(Flt target, Flt time) {
      if (!std::isfinite(target)) return *this;
      if (CleanTime(time) > 0.f) {
        (void)segments(&target, &time, 1);
        return *this;
      }
      current = target;
      Drop();
      jumped = true;
      return *this;
    }

    lineSegments& lineSegments::jump(Flt value) {
      if (!std::isfinite(value)) return *this;
      current = value;
      Drop();
      return *this;
    }

    lineSegments& lineSegments::stop() {
      Drop();
      return *this;
    }

    lineSegments& lineSegments::pause() {
      held = true;
      return *this;
    }

    lineSegments& lineSegments::resume() {
      held = false;
      return *this;
    }

    lineSegments& lineSegments::reset() {
      current = 0.f;
      Drop();
      ended = false;
      return *this;
    }

    void lineSegments::Drop() {
      count = 0;
      index = 0;
      held = false;
      jumped = false;
      elapsed = 0.0;
      from = current;
    }

    Flt lineSegments::Step() {
      if (index >= count || held) return current;
      elapsed += 1.0;
      for (;;) {
        // Read on every sample, so the segment follows the live rate.
        const Dbl length = Samples(times[index]);
        if (elapsed < length) {
          current = from + (targets[index] - from) * static_cast<Flt>(elapsed / length);
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
        from = current;
      }
    }

    buffer& lineSegments::operator()(UInt length) {
      ended = jumped;
      jumped = false;
      const UInt n = Clamp(length);
      Fit(output, n);
      Fit(busy, n);
      Flt* out = output.getPtr();
      Flt* flag = busy.getPtr();
      for (UInt i = 0; i < n; ++i) {
        out[i] = Step();
        flag[i] = active() ? 1.f : 0.f;
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
