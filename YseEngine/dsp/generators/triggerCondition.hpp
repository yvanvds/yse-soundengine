/*
  ==============================================================================

    triggerCondition.hpp
    Whether a control signal's step from one sample to the next meets one of
    the trigger conditions of Max's sah~ and stash~. Shared by
    DSP::sampleHold (#1082) and DSP::signalStash (#1084). Internal: not part
    of yse.hpp.

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_TRIGGERCONDITION_HPP
#define YSE_DSP_GENERATORS_TRIGGERCONDITION_HPP

#include "../../headers/types.hpp"
#include "phaseStep.hpp"

namespace YSE {
  namespace DSP {
    namespace triggerCondition {

      /** Whether the control step from `previous` to `current` meets `mode`,
       *  numbered as Max's trigger modes: 0 ascending through `threshold`, 1
       *  descending through it, 2 either, 3 equal to it, 4 zero to nonzero, 5
       *  nonzero to zero, 6 passing `wrappedThreshold` (the threshold wrapped
       *  into 0..1) as a phase. Any other mode is a decrease (Pure Data's
       *  samphold~). */
      inline bool Meets(int mode, Flt previous, Flt current, Flt threshold, Dbl wrappedThreshold) {
        switch (mode) {
        case 0:
          return previous <= threshold && current > threshold;
        case 1:
          return previous >= threshold && current < threshold;
        case 2:
          return (previous <= threshold && current > threshold) ||
                 (previous >= threshold && current < threshold);
        case 3:
          return current == threshold;
        case 4:
          return previous == 0.f && current != 0.f;
        case 5:
          return previous != 0.f && current == 0.f;
        case 6: {
          // As phasorWhat reads a ramp: both are phases, the step is taken
          // the short way round, and a rise passes t when a <= t < b (or
          // t + 1 past a forward wrap), a fall when b < t <= a (or t - 1 past
          // a backward wrap).
          const Dbl a = phaseStep::Wrap(static_cast<Dbl>(previous));
          const Dbl x = phaseStep::Wrap(static_cast<Dbl>(current));
          const phaseStep::step s = phaseStep::Of(static_cast<Flt>(a), static_cast<Flt>(x));
          const Dbl b = a + s.delta;
          const Dbl t = wrappedThreshold;
          if (s.delta > 0.0) return (a <= t && t < b) || (a <= t + 1.0 && t + 1.0 < b);
          if (s.delta < 0.0) return (b < t && t <= a) || (b < t - 1.0 && t - 1.0 <= a);
          return false;
        }
        default:
          return current < previous;
        }
      }

    } // namespace triggerCondition
  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_TRIGGERCONDITION_HPP
