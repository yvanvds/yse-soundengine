#include "phasorStepDiv.hpp"
#include "phaseStep.hpp"

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

  // The double ramp is below 1, but may round to 1.0f. Keep the output below
  // 1 too, at the largest float that is.
  inline Flt ToSample(Dbl p) {
    const Flt v = static_cast<Flt>(p);
    return v < 1.0f ? v : 0.99999994f;
  }

  // A step time: below 0, or not a finite number, counts as 0.
  inline Flt Time(Flt t) {
    return std::isfinite(t) && t > 0.f ? t : 0.f;
  }
} // namespace

namespace YSE {
  namespace DSP {

    phasorStepDiv::phasorStepDiv() : output(STANDARD_BUFFERSIZE), indexOutput(STANDARD_BUFFERSIZE) {
      Land();
    }

    phasorStepDiv& phasorStepDiv::pattern(const Flt* times, UInt count) {
      if (times == nullptr) count = 0;
      if (count > MAX_STEPS) count = MAX_STEPS;
      for (UInt i = 0; i < count; ++i)
        requested.times[i] = Time(times[i]);
      requested.count = count;
      pending = true;
      return *this;
    }

    phasorStepDiv& phasorStepDiv::breakpoints(const Flt* list, UInt count, bool curves) {
      if (list == nullptr) count = 0;
      const UInt stride = curves ? 3 : 2;
      Flt times[MAX_STEPS];
      UInt n = 0;
      for (UInt group = 0; n < MAX_STEPS && (group + 1) * stride <= count; ++group)
        times[n++] = list[group * stride + 1];
      return pattern(times, n);
    }

    phasorStepDiv& phasorStepDiv::onsets(const Flt* offsets, UInt count) {
      if (offsets == nullptr || count == 0) return pattern(nullptr, 0);
      if (count > MAX_STEPS - 1) count = MAX_STEPS - 1;
      Flt times[MAX_STEPS];
      Dbl reach = 0.0;
      for (UInt i = 0; i < count; ++i) {
        const Dbl o = std::isfinite(offsets[i]) ? static_cast<Dbl>(offsets[i]) : 0.0;
        if (o > reach) {
          times[i] = static_cast<Flt>(o - reach);
          reach = o;
        } else {
          times[i] = 0.f;
        }
      }
      UInt n = count;
      if (reach < 1.0) times[n++] = static_cast<Flt>(1.0 - reach);
      return pattern(times, n);
    }

    phasorStepDiv& phasorStepDiv::loop(bool on) {
      looping = on;
      return *this;
    }

    phasorStepDiv& phasorStepDiv::syncUpdate(bool on) {
      synced = on;
      return *this;
    }

    phasorStepDiv& phasorStepDiv::start() {
      Begin(0);
      return *this;
    }

    phasorStepDiv& phasorStepDiv::step(int n) {
      Begin(n < 0 ? 0u : static_cast<UInt>(n));
      return *this;
    }

    void phasorStepDiv::Land() {
      active = requested;
      Dbl sum = 0.0;
      for (UInt i = 0; i < active.count; ++i) {
        active.starts[i] = sum;
        sum += static_cast<Dbl>(active.times[i]);
      }
      active.total = sum;
      pending = false;
    }

    // A start or a jump is a sequence boundary: a waiting pattern lands.
    void phasorStepDiv::Begin(UInt from) {
      if (pending) Land();
      if (active.total <= 0.0) {
        playing = false;
        return;
      }
      cursor = from < active.count ? from : active.count - 1;
      elapsed = 0.0;
      playing = true;
    }

    Dbl phasorStepDiv::Length(UInt i) const {
      return static_cast<Dbl>(active.times[i]) * samplesPerMs;
    }

    // Moves past the steps that are over (and the ones of time 0), through
    // the end of the sequence: a loop, or a stop.
    void phasorStepDiv::Settle() {
      int loops = 0;
      while (playing) {
        if (cursor < active.count) {
          const Dbl length = Length(cursor);
          if (elapsed < length) return;
          elapsed -= length;
          ++cursor;
          continue;
        }
        if (pending) Land();
        Dbl total = 0.0;
        for (UInt i = 0; i < active.count; ++i)
          total += Length(i);
        if (looping && total > 0.0) {
          // Whole sequences shorter than a sample are skipped; a second
          // wrap in one sample can only be rounding, and restarts clean.
          elapsed = ++loops > 1 ? 0.0 : std::fmod(elapsed, total);
          cursor = 0;
          continue;
        }
        playing = false;
        heldOut = 1.f;
        heldIndex = currentIndex;
      }
    }

    Flt phasorStepDiv::Free() {
      if (pending && (!synced || !playing)) Land();
      Settle();
      if (!playing) {
        currentIndex = heldIndex;
        return heldOut;
      }
      const Flt out = ToSample(elapsed / Length(cursor));
      currentIndex = static_cast<Flt>(cursor);
      elapsed += 1.0;
      return out;
    }

    Flt phasorStepDiv::Driven(Flt in) {
      if (!std::isfinite(in)) in = previous;
      Flt x = static_cast<Flt>(phaseStep::Wrap(static_cast<Dbl>(in)));
      if (x >= 1.f) x = 0.f;

      // Settings made before the first sample apply from it.
      const bool first = !started;
      int wrap = 0;
      if (started) wrap = phaseStep::Of(previous, x).wrap;
      started = true;
      previous = x;

      if (pending && (!synced || wrap != 0 || first)) Land();

      if (active.total <= 0.0) {
        currentIndex = 0.f;
        return x;
      }
      // Steps of time 0 start where the next one does, so the scan passes
      // them: p is below the total, and the step found has a time above 0.
      const Dbl p = static_cast<Dbl>(x) * active.total;
      UInt j = 0;
      while (j + 1 < active.count && active.starts[j + 1] <= p)
        ++j;
      currentIndex = static_cast<Flt>(j);
      const Dbl t = static_cast<Dbl>(active.times[j]);
      return t > 0.0 ? ToSample((p - active.starts[j]) / t) : 0.f;
    }

    Flt phasorStepDiv::process(Flt phase) {
      return Driven(phase);
    }

    Flt phasorStepDiv::process() {
      samplesPerMs = static_cast<Dbl>(SAMPLERATE) * 0.001;
      return Free();
    }

    buffer& phasorStepDiv::operator()(buffer& phase) {
      const UInt length = Clamp(phase.getLength());
      Fit(output, length);
      Fit(indexOutput, length);
      const Flt* in = phase.getPtr();
      Flt* out = output.getPtr();
      Flt* idx = indexOutput.getPtr();
      for (UInt i = 0; i < length; ++i) {
        out[i] = Driven(in[i]);
        idx[i] = currentIndex;
      }
      return output;
    }

    buffer& phasorStepDiv::operator()() {
      const UInt length = STANDARD_BUFFERSIZE;
      Fit(output, length);
      Fit(indexOutput, length);
      samplesPerMs = static_cast<Dbl>(SAMPLERATE) * 0.001;
      Flt* out = output.getPtr();
      Flt* idx = indexOutput.getPtr();
      for (UInt i = 0; i < length; ++i) {
        out[i] = Free();
        idx[i] = currentIndex;
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
