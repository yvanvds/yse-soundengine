#include "snowfall.hpp"
#include "whiteSource.hpp"

#include <cmath>

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  inline UInt Dimensions(UInt n) {
    if (n < 1) return 1;
    return n > YSE::DSP::snowfall::MAX_DIMENSIONS ? YSE::DSP::snowfall::MAX_DIMENSIONS : n;
  }

  // A finite `value` into `to`; anything else changes nothing.
  inline void SetFinite(Flt& to, Flt value) {
    if (std::isfinite(value)) to = value;
  }
} // namespace

namespace YSE {
  namespace DSP {

    snowfall::snowfall(UInt dimensions)
      : count(Dimensions(dimensions)), state(detail::WHITE_DEFAULT_SEED) {}

    snowfall& snowfall::dimensions(UInt n) {
      count = Dimensions(n);
      return *this;
    }

#define YSE_SNOWFALL_SETTER(name, field)                                                           \
  snowfall& snowfall::name(UInt d, Flt value) {                                                    \
    if (d < MAX_DIMENSIONS) SetFinite(dims[d].field, value);                                       \
    return *this;                                                                                  \
  }

    YSE_SNOWFALL_SETTER(direction, direction)
    YSE_SNOWFALL_SETTER(directionDeviation, directionDeviation)
    YSE_SNOWFALL_SETTER(energyRamp, energy)
    YSE_SNOWFALL_SETTER(initial, initial)
    YSE_SNOWFALL_SETTER(initialDeviation, initialDeviation)
    YSE_SNOWFALL_SETTER(minimum, minimum)
    YSE_SNOWFALL_SETTER(maximum, maximum)
    YSE_SNOWFALL_SETTER(scaleMinimum, scaleMinimum)
    YSE_SNOWFALL_SETTER(scaleMaximum, scaleMaximum)
    YSE_SNOWFALL_SETTER(squish, squish)
    YSE_SNOWFALL_SETTER(endValue, endValue)

#undef YSE_SNOWFALL_SETTER

    snowfall& snowfall::wanderProbability(UInt d, Flt probability) {
      if (d >= MAX_DIMENSIONS || !std::isfinite(probability)) return *this;
      if (probability < 0.f) probability = 0.f;
      if (probability > 1.f) probability = 1.f;
      dims[d].wander = probability;
      return *this;
    }

    snowfall& snowfall::interval(Flt ms) {
      SetFinite(intervalMs, ms);
      return *this;
    }

    snowfall& snowfall::intervalDeviation(Flt ms) {
      if (std::isfinite(ms) && ms >= 0.f) intervalDev = ms;
      return *this;
    }

    snowfall& snowfall::boundaryMode(BOUNDARY_MODE m) {
      if (m >= BOUNCE && m <= CLIP) boundary = m;
      return *this;
    }

    snowfall& snowfall::endMode(END_MODE m) {
      if (m == END_VALUE || m == CURRENT_VALUE) ending = m;
      return *this;
    }

    snowfall& snowfall::startMode(START_MODE m) {
      if (m == INITIAL || m == CURRENT) starting = m;
      return *this;
    }

    snowfall& snowfall::seed(std::uint32_t value) {
      state = value;
      return *this;
    }

    Dbl snowfall::Random() {
      return detail::NextWhite(state);
    }

    Dbl snowfall::Probability() {
      return (detail::NextWhite(state) + 1.0) * 0.5;
    }

    Dbl snowfall::NextInterval() {
      // Drawn even without a deviation, so the order of draws is fixed.
      const Dbl r = Random();
      return static_cast<Dbl>(intervalMs) + static_cast<Dbl>(intervalDev) * r;
    }

    void snowfall::Start() {
      for (UInt i = 0; i < count; ++i) {
        dimension& d = dims[i];
        const Dbl ri = Random();
        const Dbl rv = Random();
        if (starting == INITIAL || !started) {
          const Dbl lo = d.minimum < d.maximum ? d.minimum : d.maximum;
          const Dbl hi = d.minimum < d.maximum ? d.maximum : d.minimum;
          Dbl x = static_cast<Dbl>(d.initial) + static_cast<Dbl>(d.initialDeviation) * ri;
          if (x < lo) x = lo;
          if (x > hi) x = hi;
          d.x = x;
        }
        d.reversed = false;
        d.v = static_cast<Dbl>(d.direction) + static_cast<Dbl>(d.directionDeviation) * rv;
      }
      untilUpdate = NextInterval();
      living = true;
      started = true;
    }

    void snowfall::Update() {
      for (UInt i = 0; i < count; ++i) {
        dimension& d = dims[i];
        const Dbl rv = Random();
        const Dbl u = Probability();
        Dbl v = static_cast<Dbl>(d.direction) + static_cast<Dbl>(d.directionDeviation) * rv;
        if (d.reversed) v = -v;
        if (u < static_cast<Dbl>(d.wander)) {
          d.reversed = !d.reversed;
          v = -v;
        }
        d.v = v;
      }
    }

    bool snowfall::Move(dimension& d, Dbl ramp, Dbl rate) {
      const Dbl step = d.v * (1.0 + static_cast<Dbl>(d.energy) * ramp) / rate;
      const Dbl lo = d.minimum < d.maximum ? d.minimum : d.maximum;
      const Dbl hi = d.minimum < d.maximum ? d.maximum : d.minimum;
      Dbl x = d.x + step;
      if (x >= lo && x <= hi) {
        d.x = x;
        return true;
      }
      const Dbl span = hi - lo;
      switch (boundary) {
      case BOUNCE:
        if (span > 0.0) {
          x = x > hi ? hi - (x - hi) : lo + (lo - x);
          // A reversal only when the step pushed outward.
          if ((d.x + step > hi && step > 0.0) || (d.x + step < lo && step < 0.0)) {
            d.v = -d.v * static_cast<Dbl>(d.squish);
            d.reversed = !d.reversed;
          }
        }
        break;
      case END:
        d.x = x > hi ? hi : lo;
        return false;
      case WRAP:
        if (span > 0.0) {
          x = std::fmod(x - lo, span);
          if (x < 0.0) x += span;
          x += lo;
        }
        break;
      case CLIP:
        break;
      }
      if (x < lo) x = lo;
      if (x > hi) x = hi;
      d.x = x;
      return true;
    }

    Flt snowfall::Scaled(const dimension& d) const {
      const Dbl span = static_cast<Dbl>(d.maximum) - static_cast<Dbl>(d.minimum);
      if (span == 0.0) return d.scaleMinimum;
      const Dbl t = (d.x - static_cast<Dbl>(d.minimum)) / span;
      const Dbl s0 = static_cast<Dbl>(d.scaleMinimum);
      const Dbl s1 = static_cast<Dbl>(d.scaleMaximum);
      return static_cast<Flt>(s0 + t * (s1 - s0));
    }

    void snowfall::process(Flt ramp, Flt* out) {
      if (!std::isfinite(ramp)) ramp = last;
      const bool wrapped = static_cast<Dbl>(ramp) < static_cast<Dbl>(last) - 0.5;
      if (living && (ramp <= 0.f || wrapped)) living = false;
      if (ramp > 0.f && (last <= 0.f || wrapped)) Start();
      last = ramp;

      if (living) {
        const Dbl rate = static_cast<Dbl>(SAMPLERATE);
        bool ended = false;
        for (UInt i = 0; i < count; ++i) {
          if (!Move(dims[i], static_cast<Dbl>(ramp), rate)) ended = true;
        }
        if (intervalMs > 0.f) {
          untilUpdate -= 1000.0 / rate;
          // At most one update a sample, however short the interval.
          if (untilUpdate <= 0.0) {
            Update();
            const Dbl next = NextInterval();
            untilUpdate = next > 0.0 ? next : 0.0;
          }
        }
        for (UInt i = 0; i < count; ++i)
          dims[i].held = Scaled(dims[i]);
        if (ended) living = false;
        for (UInt i = 0; i < count; ++i)
          out[i] = dims[i].held;
        return;
      }
      for (UInt i = 0; i < count; ++i)
        out[i] = ending == END_VALUE ? dims[i].endValue : dims[i].held;
    }

    buffer& snowfall::operator()(buffer& ramp) {
      const UInt n = Clamp(ramp.getLength());
      Flt* out[MAX_DIMENSIONS] = {};
      for (UInt d = 0; d < MAX_DIMENSIONS; ++d) {
        // Within the capacity set in the constructor: no allocation.
        if (outputs[d].getLength() != n) outputs[d].resize(n);
        out[d] = outputs[d].getPtr();
      }
      const Flt* in = ramp.getPtr();
      const UInt dims = count;
      Flt values[MAX_DIMENSIONS] = {0.f, 0.f, 0.f, 0.f};
      for (UInt i = 0; i < n; ++i) {
        process(in[i], values);
        for (UInt d = 0; d < dims; ++d)
          out[d][i] = values[d];
      }
      return outputs[0];
    }

  } // namespace DSP
} // namespace YSE
