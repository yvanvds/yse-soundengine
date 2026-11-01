#include "pulseTrain.hpp"

#include <cmath>

namespace {
  // Wrap into [0, 1); the guard catches a value a hair below 0 that rounds
  // to 1.
  inline Dbl Wrap(Dbl p) {
    p -= std::floor(p);
    return p >= 1.0 ? 0.0 : p;
  }

  inline Dbl CleanPhase(Flt value) {
    return std::isfinite(value) ? Wrap(static_cast<Dbl>(value)) : 0.0;
  }

  inline Dbl CleanWidth(Flt value) {
    if (std::isnan(value)) return 0.5;
    if (value < 0.f) return 0.0;
    if (value > 1.f) return 1.0;
    return static_cast<Dbl>(value);
  }

  // The period in samples for a period in ms at the live rate; 0 stops.
  inline Dbl Samples(Flt ms) {
    if (!(ms > 0.f) || !std::isfinite(ms) || YSE::SAMPLERATE <= 0) return 0.0;
    return static_cast<Dbl>(ms) * static_cast<Dbl>(YSE::SAMPLERATE) / 1000.0;
  }

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    pulseTrain::pulseTrain()
      : intervalMs(1000.f),
        pulseWidth(0.5f),
        offset(0.f),
        defer(false),
        kick(false),
        cycleStart(true),
        on(false),
        running(false),
        elapsed(0.0),
        period(0.0),
        count(0),
        pos{},
        output(STANDARD_BUFFERSIZE) {}

    // Switches to a period of `target` samples (0 stops) at the same
    // position in the cycle.
    void pulseTrain::retime(Dbl target) {
      if (target <= 0.0) {
        running = false;
        return;
      }
      if (period > 0.0 && target != period) {
        elapsed *= target / period;
        if (elapsed >= target) elapsed = 0.0;
      }
      period = target;
      running = true;
    }

    Flt pulseTrain::step(Flt ms, Flt w, Flt ph, UInt index) {
      const bool kicked = kick;
      if (kicked) {
        // The on part starts here: a new cycle, an onset whatever came before.
        kick = false;
        on = false;
        cycleStart = true;
      }

      // Deferred, the interval is read only when a cycle starts or while
      // stopped. The period is counted in samples, so a whole number of
      // samples is exact.
      if (!defer || cycleStart || !running) retime(Samples(ms));
      cycleStart = false;

      const Dbl shift = CleanPhase(ph) * period;
      if (kicked) elapsed = shift;

      // Samples into the pulse's own cycle, counted from its onset.
      Dbl at = elapsed - shift;
      if (at < 0.0) at += period;
      if (at >= period) at = 0.0;

      // At least one sample on and one off per cycle: any stretch of the
      // cycle one sample long holds exactly one sample.
      Dbl high = CleanWidth(w) * period;
      if (high > period - 1.0) high = period - 1.0;
      if (high < 1.0) high = 1.0;

      const bool now = at < high;
      if (now && !on) pos[count++] = index;
      on = now;

      if (running) {
        elapsed += 1.0;
        if (elapsed >= period) {
          elapsed -= period;
          // A period below one sample.
          if (elapsed >= period) elapsed = std::fmod(elapsed, period);
          cycleStart = true;
        }
      }
      return now ? 1.f : 0.f;
    }

    buffer& pulseTrain::operator()(UInt length) {
      length = Clamp(length);
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);
      Flt* out = output.getPtr();
      count = 0;
      for (UInt i = 0; i < length; ++i)
        out[i] = step(intervalMs, pulseWidth, offset, i);
      return output;
    }

    buffer& pulseTrain::operator()(buffer& periods) {
      const UInt length = Clamp(periods.getLength());
      if (output.getLength() != length) output.resize(length);
      const Flt* ms = periods.getPtr();
      Flt* out = output.getPtr();
      count = 0;
      for (UInt i = 0; i < length; ++i)
        out[i] = step(ms[i], pulseWidth, offset, i);
      return output;
    }

    buffer& pulseTrain::operator()(buffer& periods, buffer& widths, buffer& phases) {
      UInt length = periods.getLength();
      if (widths.getLength() < length) length = widths.getLength();
      if (phases.getLength() < length) length = phases.getLength();
      length = Clamp(length);
      if (output.getLength() != length) output.resize(length);
      const Flt* ms = periods.getPtr();
      const Flt* w = widths.getPtr();
      const Flt* ph = phases.getPtr();
      Flt* out = output.getPtr();
      count = 0;
      for (UInt i = 0; i < length; ++i)
        out[i] = step(ms[i], w[i], ph[i], i);
      return output;
    }

    UInt pulseTrain::onsets() const {
      return count;
    }

    UInt pulseTrain::onset(UInt i) const {
      return pos[i];
    }

    pulseTrain& pulseTrain::interval(Flt ms) {
      intervalMs = (std::isnan(ms) || ms < 0.f) ? 0.f : ms;
      return *this;
    }

    Flt pulseTrain::interval() const {
      return intervalMs;
    }

    pulseTrain& pulseTrain::width(Flt value) {
      pulseWidth = static_cast<Flt>(CleanWidth(value));
      return *this;
    }

    Flt pulseTrain::width() const {
      return pulseWidth;
    }

    pulseTrain& pulseTrain::phase(Flt value) {
      offset = static_cast<Flt>(CleanPhase(value));
      // The float nearest a phase just below 1 can be 1; 1 is the same as 0.
      if (offset >= 1.f) offset = 0.f;
      return *this;
    }

    Flt pulseTrain::phase() const {
      return offset;
    }

    pulseTrain& pulseTrain::deferInterval(bool value) {
      defer = value;
      return *this;
    }

    bool pulseTrain::deferInterval() const {
      return defer;
    }

    pulseTrain& pulseTrain::trigger() {
      kick = true;
      return *this;
    }

    pulseTrain& pulseTrain::reset() {
      kick = false;
      cycleStart = true;
      on = false;
      running = false;
      elapsed = 0.0;
      period = 0.0;
      count = 0;
      return *this;
    }

  } // namespace DSP
} // namespace YSE
