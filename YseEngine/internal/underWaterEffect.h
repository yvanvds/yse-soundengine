/*
  ==============================================================================

    underWaterEffect.h
    Created: 1 Feb 2014 10:02:28pm
    Author:  yvan

    Re-expressed for issue #327: the DSP moved into the ordinary insert
    module DSP::MODULES::underWater; this class is now only the engine-side
    *driver* that binds the module to its default spatial control.

  ==============================================================================
*/

#ifndef UNDERWATEREFFECT_H_INCLUDED
#define UNDERWATEREFFECT_H_INCLUDED

#include "../classes.hpp"
#include "../dsp/modules/underWater.hpp"
#include "../reverb/reverbInterface.hpp"

namespace YSE {
  class channel;

  namespace INTERNAL {

    /**
      The engine-side driver for the stock underwater effect (issue #327).

      The DSP itself is an ordinary insert module (DSP::MODULES::underWater);
      this class owns the engine's default instance plus the matching
      REVERB_UNDERWATER zone, and binds both to the spatial control the public
      API exposes: listener depth below the water plane.

      - attach() places the module at the head of the target channel's insert
        chain through the ordinary channel::setDSP message path — the channel
        implementation carries no underwater knowledge any more.
      - setDepth() is the control-rate default driver: it writes the module's
        depth parameter (a wait-free atomic store the audio thread picks up on
        the next block) and toggles/positions the underwater reverb zone,
        exactly as the legacy hard-wired path did.

      User code that wants a different binding (a slider, a patcher ramp, a
      live-coded expression) instantiates its own DSP::MODULES::underWater and
      drives depth() directly — same module, different drivers.
    */
    class underWaterEffect {
    public:
      underWaterEffect();

      /** Attach the engine's module instance to `target`'s insert slot via
       *  the ordinary channel::setDSP path. Only one channel carries the
       *  stock effect at a time: re-attaching to a different channel severs
       *  the previous link first. */
      underWaterEffect& attach(const channel& target);

      /** The default spatial driver: listener depth below the water plane,
       *  evaluated at control rate by the caller. Drives the module's depth
       *  parameter and the underwater reverb zone. */
      underWaterEffect& setDepth(Flt value);

      /** The engine's default module instance (exposed for tests). */
      DSP::MODULES::underWater& module();

    private:
      DSP::MODULES::underWater fx;
      reverb verb;
      const channel* lastTarget; // identity comparison only, never dereferenced
    };

    underWaterEffect& UnderWaterEffect();

  } // namespace INTERNAL
} // namespace YSE

#endif // UNDERWATEREFFECT_H_INCLUDED
