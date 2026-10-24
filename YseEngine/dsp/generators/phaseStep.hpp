/*
  ==============================================================================

    phaseStep.hpp
    How far a 0..1 phase input moved between two samples, and whether it
    wrapped. Shared by the classes that follow a phasor input (phasorRate,
    #1073, and the rest of the phasor toolkit). Internal: not part of yse.hpp.

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_PHASESTEP_HPP
#define YSE_DSP_GENERATORS_PHASESTEP_HPP

#include <cmath>
#include "../../headers/types.hpp"

namespace YSE {
  namespace DSP {
    namespace phaseStep {

      /** One input step: the distance moved, in cycles, and the wrap it
       *  crossed: +1 forward through 1 -> 0, -1 backward through 0 -> 1, 0
       *  none. */
      struct step {
        Dbl delta;
        int wrap;
      };

      /** The step from `previous` to `current`. A jump of more than half a
       *  cycle is read as a wrap the short way round, so a rising ramp that
       *  falls from 0.98 to 0.01 moved +0.03 and wrapped forward. */
      inline step Of(Flt previous, Flt current) {
        Dbl delta = static_cast<Dbl>(current) - static_cast<Dbl>(previous);
        if (delta < -0.5) return {delta + 1.0, 1};
        if (delta > 0.5) return {delta - 1.0, -1};
        return {delta, 0};
      }

      /** True when a step from `previous` to `current` with `wrap` passed the
       *  phase `at` (0..1), in the direction it moved. A forward wrap passes
       *  0 (and 1); a step that does not move passes nothing. */
      inline bool Passes(Flt previous, Flt current, int wrap, Flt at) {
        if (wrap > 0) return at > previous || at <= current;
        if (wrap < 0) return at < previous || at >= current;
        if (current > previous) return previous < at && at <= current;
        if (current < previous) return current <= at && at < previous;
        return false;
      }

      /** `p` wrapped into [0, 1). The guard catches the one input where
       *  rounding lands exactly on 1: a value a hair below 0. */
      inline Dbl Wrap(Dbl p) {
        p -= std::floor(p);
        return p >= 1.0 ? 0.0 : p;
      }

    } // namespace phaseStep
  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_PHASESTEP_HPP
