#include "triggeredRamp.hpp"
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

  // Sample `i` of `b`, or `fallback` past its end, without one, or for a
  // sample that is not a finite number.
  inline Flt Read(buffer* b, UInt i, Flt fallback) {
    if (b == nullptr || i >= b->getLength()) return fallback;
    const Flt value = b->getPtr()[i];
    return std::isfinite(value) ? value : fallback;
  }
} // namespace

namespace YSE {
  namespace DSP {

    triggeredRamp::triggeredRamp() : output(STANDARD_BUFFERSIZE) {}

    triggeredRamp& triggeredRamp::duration(Flt ms) {
      if (std::isfinite(ms)) durationMs = ms > 0.f ? ms : 0.f;
      return *this;
    }

    triggeredRamp& triggeredRamp::start(Flt value) {
      if (std::isfinite(value)) startValue = value;
      return *this;
    }

    triggeredRamp& triggeredRamp::end(Flt value) {
      if (std::isfinite(value)) endValue = value;
      return *this;
    }

    triggeredRamp& triggeredRamp::curve(Flt value) {
      if (!std::isfinite(value)) return *this;
      if (value < -1.f) value = -1.f;
      if (value > 1.f) value = 1.f;
      curvature = value;
      return *this;
    }

    triggeredRamp& triggeredRamp::mode(durationMode value) {
      durationRead = value;
      return *this;
    }

    triggeredRamp& triggeredRamp::resetToZero(bool on) {
      zeroWhenIdle = on;
      return *this;
    }

    triggeredRamp& triggeredRamp::retrigger(bool on) {
      restartWhileRunning = on;
      return *this;
    }

    buffer& triggeredRamp::operator()(buffer& trigger) {
      return Process(trigger, nullptr, nullptr, nullptr);
    }

    buffer& triggeredRamp::operator()(buffer& trigger, buffer& duration, buffer& start,
                                      buffer& end) {
      return Process(trigger, &duration, &start, &end);
    }

    buffer& triggeredRamp::Process(buffer& trigger, buffer* durations, buffer* starts,
                                   buffer* ends) {
      const UInt length = Clamp(trigger.getLength());
      Fit(output, length);
      // The live rate, once per block (#634).
      const double samplesPerMs = static_cast<double>(SAMPLERATE) * 0.001;
      const Flt exponent = curveBend::Exponent(curvature);
      const bool continuous = durationRead == durationMode::continuous;
      const Flt* in = trigger.getPtr();
      Flt* out = output.getPtr();
      for (UInt i = 0; i < length; ++i) {
        Flt x = in[i];
        if (!std::isfinite(x)) x = 0.f;
        const bool triggered = previous == 0.f && x != 0.f;
        previous = x;

        if (triggered && (state != RUNNING || restartWhileRunning)) {
          state = RUNNING;
          elapsed = 0.0;
          position = 0.0;
          anchorSample = 0.0;
          anchorPhase = 0.0;
          anchorLength = static_cast<double>(Read(durations, i, durationMs)) * samplesPerMs;
        } else if (state == RUNNING) {
          elapsed += 1.0;
          if (continuous) {
            const double samples =
                static_cast<double>(Read(durations, i, durationMs)) * samplesPerMs;
            if (samples != anchorLength) {
              // Keep the phase reached on the last sample; stretch the rest.
              anchorSample = elapsed - 1.0;
              anchorPhase = position;
              anchorLength = samples;
            }
          }
        }

        const Flt from = Read(starts, i, startValue);
        const Flt to = Read(ends, i, endValue);
        if (state == RUNNING) {
          if (elapsed >= anchorLength) {
            position = 1.0;
            state = DONE;
            out[i] = to;
            continue;
          }
          // anchorLength > elapsed > anchorSample here: the span is positive.
          position = anchorPhase +
                     (1.0 - anchorPhase) * (elapsed - anchorSample) / (anchorLength - anchorSample);
          const Flt f = curveBend::Bend(static_cast<Flt>(position), exponent);
          out[i] = from + (to - from) * f;
        } else if (zeroWhenIdle) {
          out[i] = 0.f;
        } else {
          out[i] = state == DONE ? to : from;
        }
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
