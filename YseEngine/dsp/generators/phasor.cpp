#include "phasor.hpp"

#include <cmath>

namespace {
  // Wrap into [0, 1). floor keeps negative phases (a ramp running backwards)
  // on the same cycle grid as positive ones. The guard catches the one input
  // where rounding lands exactly on 1: a value a hair below 0.
  inline Dbl Wrap(Dbl p) {
    p -= std::floor(p);
    return p >= 1.0 ? 0.0 : p;
  }

  // The double phase is below 1, but the nearest float to 0.99999999 is 1.0f.
  // Keep the output below 1 too, at the largest float that is.
  inline Flt ToSample(Dbl p) {
    const Flt v = static_cast<Flt>(p);
    return v < 1.0f ? v : 0.99999994f;
  }

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    phasor::phasor() : current(0.0), output(STANDARD_BUFFERSIZE) {}

    phasor& phasor::phase(Flt value) {
      current = Wrap(static_cast<Dbl>(value));
      return *this;
    }

    Flt phasor::phase() const {
      return ToSample(current);
    }

    phasor& phasor::syncUpdate(bool on) {
      syncOn = on;
      return *this;
    }

    phasor& phasor::update() {
      takeNow = true;
      return *this;
    }

    Dbl phasor::Take(Dbl& held, Dbl wanted, bool& known) {
      if (!syncOn || !known || takeNow) {
        held = wanted;
        known = true;
      }
      return held;
    }

    buffer& phasor::operator()(Flt frequency, UInt length) {
      length = Clamp(length);
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);

      const Dbl wanted = static_cast<Dbl>(frequency);
      const Dbl toIncrement = 1.0 / static_cast<Dbl>(SAMPLERATE);
      Dbl increment = Take(heldFrequency, wanted, haveFrequency) * toIncrement;
      takeNow = false;
      Flt* out = output.getPtr();
      Dbl p = current;
      for (UInt i = 0; i < length; ++i) {
        out[i] = ToSample(p);
        const Dbl next = p + increment;
        p = Wrap(next);
        // Sync update: a held frequency is taken as the ramp wraps.
        if (syncOn && (next >= 1.0 || next < 0.0)) {
          heldFrequency = wanted;
          increment = wanted * toIncrement;
        }
      }
      current = p;
      return output;
    }

    buffer& phasor::operator()(buffer& frequency) {
      const UInt length = Clamp(frequency.getLength());
      if (output.getLength() != length) output.resize(length);

      const Dbl toIncrement = 1.0 / static_cast<Dbl>(SAMPLERATE);
      const Flt* in = frequency.getPtr();
      Flt* out = output.getPtr();
      Dbl p = current;
      for (UInt i = 0; i < length; ++i) {
        out[i] = ToSample(p);
        p = Wrap(p + static_cast<Dbl>(in[i]) * toIncrement);
      }
      current = p;
      return output;
    }

    buffer& phasor::operator()(const beatSpan& clock, Dbl periodBeats, bool lock, Flt offset) {
      const UInt length = Clamp(clock.length() > 0 ? clock.length() : STANDARD_BUFFERSIZE);
      if (output.getLength() != length) output.resize(length);
      Flt* out = output.getPtr();

      const bool usable = clock.valid() && std::isfinite(periodBeats) && periodBeats > 0.0;
      if (!usable) {
        // No clock to follow, or no period: hold.
        const Flt held = ToSample(current);
        for (UInt i = 0; i < length; ++i)
          out[i] = held;
        takeNow = false;
        return output;
      }

      if (lock) {
        // The phase is the clock's position: nothing of the ramp's own is
        // kept, so every locked ramp on one clock agrees. A new period is
        // taken at once, since a locked ramp has no cycle of its own to finish.
        heldPeriod = periodBeats;
        havePeriod = true;
        takeNow = false;
        const Dbl shift = std::isfinite(offset) ? static_cast<Dbl>(offset) : 0.0;
        const Dbl perBeat = 1.0 / periodBeats;
        for (UInt i = 0; i < length; ++i)
          out[i] = ToSample(Wrap(clock.at(i) * perBeat + shift));
        current = Wrap(clock.end() * perBeat + shift);
        return output;
      }

      // Unlocked: the ramp's own phase, moved by the beats the clock moves.
      Dbl increment = clock.step() / Take(heldPeriod, periodBeats, havePeriod);
      takeNow = false;
      Dbl p = current;
      for (UInt i = 0; i < length; ++i) {
        out[i] = ToSample(p);
        const Dbl next = p + increment;
        p = Wrap(next);
        if (syncOn && (next >= 1.0 || next < 0.0)) {
          heldPeriod = periodBeats;
          increment = clock.step() / periodBeats;
        }
      }
      current = p;
      return output;
    }

  } // namespace DSP
} // namespace YSE
