#include "movingAverage.hpp"

#include <cmath>

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  inline UInt ClampCapacity(UInt capacity) {
    if (capacity < 1) return 1;
    return capacity > YSE::DSP::movingAverage::MAXIMUM_CAPACITY
               ? YSE::DSP::movingAverage::MAXIMUM_CAPACITY
               : capacity;
  }
} // namespace

namespace YSE {
  namespace DSP {

    movingAverage::movingAverage(UInt capacity)
      : history(ClampCapacity(capacity), 0.f),
        cap(ClampCapacity(capacity)),
        len(cap),
        pos(0),
        how(BIPOLAR),
        sum(0.0),
        fresh(0.0),
        filled(0),
        latest(0.f),
        out(STANDARD_BUFFERSIZE) {}

    Dbl movingAverage::contribution(Flt x) const {
      const auto d = static_cast<Dbl>(x);
      switch (how) {
      case ABSOLUTE:
        return std::fabs(d);
      case RMS:
        return d * d;
      case BIPOLAR:
      default:
        return d;
      }
    }

    Flt movingAverage::average() const {
      const Dbl mean = sum / static_cast<Dbl>(len);
      if (how == RMS) return static_cast<Flt>(std::sqrt(mean > 0.0 ? mean : 0.0));
      return static_cast<Flt>(mean);
    }

    // The sum over the window that ends on the last sample taken, from
    // scratch: at most cap steps, no allocation.
    void movingAverage::recompute() {
      Dbl total = 0.0;
      UInt at = pos;
      for (UInt i = 0; i < len; ++i) {
        at = at == 0 ? cap - 1 : at - 1;
        total += contribution(history[at]);
      }
      sum = total;
      fresh = 0.0;
      filled = 0;
      latest = average();
    }

    Flt movingAverage::process(Flt in) {
      // NaN and infinities count as 0.
      const Flt x = std::isfinite(in) ? in : 0.f;
      // The sample leaving the window; with len == cap it is the one being
      // overwritten, so read it first.
      const UInt leaving = pos >= len ? pos - len : pos + cap - len;
      const Dbl added = contribution(x);
      sum += added - contribution(history[leaving]);
      history[pos] = x;
      pos = pos + 1 == cap ? 0 : pos + 1;
      // Every len samples, fresh holds exactly the window: start the
      // running sum over from it so rounding cannot drift.
      fresh += added;
      if (++filled == len) {
        sum = fresh;
        fresh = 0.0;
        filled = 0;
      }
      latest = average();
      return latest;
    }

    buffer& movingAverage::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      // Within the capacity set in the constructor: no allocation.
      if (out.getLength() != length) out.resize(length);
      const Flt* a = in.getPtr();
      Flt* o = out.getPtr();
      for (UInt i = 0; i < length; ++i)
        o[i] = process(a[i]);
      return out;
    }

    buffer& movingAverage::output() {
      return out;
    }

    Flt movingAverage::value() const {
      return latest;
    }

    movingAverage& movingAverage::length(UInt samples) {
      const UInt clean = samples < 1 ? 1 : (samples > cap ? cap : samples);
      if (clean != len) {
        len = clean;
        recompute();
      }
      return *this;
    }

    UInt movingAverage::length() const {
      return len;
    }

    UInt movingAverage::capacity() const {
      return cap;
    }

    movingAverage& movingAverage::mode(MODE m) {
      const MODE clean = (m == ABSOLUTE || m == RMS) ? m : BIPOLAR;
      if (clean != how) {
        how = clean;
        recompute();
      }
      return *this;
    }

    movingAverage::MODE movingAverage::mode() const {
      return how;
    }

    movingAverage& movingAverage::reset() {
      for (Flt& h : history)
        h = 0.f;
      pos = 0;
      sum = 0.0;
      fresh = 0.0;
      filled = 0;
      latest = 0.f;
      return *this;
    }

  } // namespace DSP
} // namespace YSE
