#include "peakTracker.hpp"

#include <cmath>

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    peakTracker::peakTracker()
      : intervalMs(0.f),
        keepSign(false),
        best(0.f),
        bestAbs(0.f),
        elapsed(0.0),
        latest(0.f),
        count(0),
        pos{},
        values{} {}

    // The interval in samples at the live rate, at least one; 0 when off.
    Dbl peakTracker::period() const {
      if (intervalMs <= 0.f || SAMPLERATE <= 0) return 0.0;
      const Dbl samples = static_cast<Dbl>(intervalMs) * static_cast<Dbl>(SAMPLERATE) / 1000.0;
      return samples < 1.0 ? 1.0 : samples;
    }

    void peakTracker::track(Flt in) {
      // NaN compares false and is skipped.
      const Flt magnitude = std::fabs(in);
      if (magnitude > bestAbs) {
        bestAbs = magnitude;
        best = in;
      }
    }

    // Counts one sample toward the interval; true when a report falls on it.
    bool peakTracker::tick(Dbl samples) {
      if (samples <= 0.0) return false;
      elapsed += 1.0;
      if (elapsed < samples) return false;
      // Keep the fraction so a fractional period does not drift; a period
      // that just shrank below the window starts over.
      elapsed -= samples;
      if (elapsed >= samples) elapsed = 0.0;
      return true;
    }

    Flt peakTracker::take() {
      latest = keepSign ? best : bestAbs;
      best = 0.f;
      bestAbs = 0.f;
      elapsed = 0.0;
      return latest;
    }

    bool peakTracker::process(Flt in) {
      track(in);
      if (!tick(period())) return false;
      // take() restarts the interval; keep the carried fraction instead.
      const Dbl carried = elapsed;
      take();
      elapsed = carried;
      return true;
    }

    UInt peakTracker::operator()(buffer& in) {
      const UInt length = Clamp(in.getLength());
      const Flt* a = in.getPtr();
      const Dbl samples = period();
      UInt c = 0;
      for (UInt i = 0; i < length; ++i) {
        track(a[i]);
        if (tick(samples)) {
          const Dbl carried = elapsed;
          values[c] = take();
          elapsed = carried;
          pos[c] = i;
          c++;
        }
      }
      count = c;
      return c;
    }

    UInt peakTracker::reports() const {
      return count;
    }

    UInt peakTracker::report(UInt i) const {
      return pos[i];
    }

    Flt peakTracker::value(UInt i) const {
      return values[i];
    }

    Flt peakTracker::lastReport() const {
      return latest;
    }

    Flt peakTracker::peak() const {
      return keepSign ? best : bestAbs;
    }

    peakTracker& peakTracker::interval(Flt ms) {
      const Flt clean = (std::isnan(ms) || ms < 0.f) ? 0.f : ms;
      if (clean != intervalMs) {
        intervalMs = clean;
        elapsed = 0.0;
      }
      return *this;
    }

    Flt peakTracker::interval() const {
      return intervalMs;
    }

    peakTracker& peakTracker::signedPeak(bool on) {
      keepSign = on;
      return *this;
    }

    bool peakTracker::signedPeak() const {
      return keepSign;
    }

    peakTracker& peakTracker::reset() {
      best = 0.f;
      bestAbs = 0.f;
      elapsed = 0.0;
      latest = 0.f;
      count = 0;
      return *this;
    }

  } // namespace DSP
} // namespace YSE
