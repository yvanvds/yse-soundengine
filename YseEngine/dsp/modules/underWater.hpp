/*
  ==============================================================================

    underWater.hpp
    Underwater effect module — the channel-path special case re-expressed as
    an ordinary insert (issue #327).

  ==============================================================================
*/

#ifndef UNDERWATER_HPP_INCLUDED
#define UNDERWATER_HPP_INCLUDED

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../dspObject.hpp"
#include "../buffer.hpp"
#include "../filters.hpp"

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /**
       *  @brief The underwater treatment as an ordinary chainable ``dspObject``.
       *
       *  This is the DSP half of the engine's classic underwater effect
       *  (issue #327), extracted from the hard-wired slot it used to occupy in
       *  the channel path. Sound underwater is more position neutral — the
       *  speed of sound is much higher, so the ear cannot tell what direction
       *  it comes from. The module therefore mixes all channels down to a
       *  position-neutral average, darkens it with a depth-driven low-pass,
       *  and crossfades the original image toward that neutral version as the
       *  listener sinks.
       *
       *  ### The depth control
       *
       *  ``depth`` is the distance below the water surface, in world distance
       *  units (meters):
       *
       *  - ``depth <= 1``: transparent — the buffer passes through untouched.
       *  - ``1 < depth < 5``: the positioned signal is progressively replaced
       *    by the low-passed neutral mix (linear crossfade over the range).
       *  - ``depth >= 5``: position information is discarded entirely; every
       *    channel carries the same low-passed mix.
       *
       *  The low-pass cutoff falls with depth as ``MidiToFreq(140 - 5 * depth)``
       *  and is floored at 200 Hz — the same curve the legacy channel-path
       *  effect has always applied.
       *
       *  ### Drivers
       *
       *  ``depth`` is deliberately driver-agnostic: writes are plain atomic
       *  stores (allocation-free, wait-free), callable from any control thread
       *  at control rate — a game tick, a patcher ramp, a live-coded
       *  expression. The classic spatial binding — listener depth below the
       *  water plane, plus the matching ``REVERB_UNDERWATER`` zone — remains
       *  the *default* driver through ``System().underWaterFX()`` /
       *  ``System().setUnderWaterDepth()``, which drive the engine's own
       *  instance of this module (see ``INTERNAL::underWaterEffect``). Same
       *  module, different drivers — the idiom established by
       *  ``morphingReverb`` (issue #326).
       *
       *  ### Placement
       *
       *  Attach it with ``YSE::channel::setDSP`` / ``YSE::sound::setDSP``, on
       *  any channel, return bus, or sound — it is no longer welded to the
       *  channel implementation. Note one deliberate difference from the
       *  legacy hard-wired slot: an insert runs *pre-fader, before* the global
       *  manager reverb, whereas the old slot ran after it. When the module
       *  and the (legacy-bound) global reverb sit on the same channel the
       *  reverb tail is no longer low-passed; place the module after a reverb
       *  insert (``dspObject::link``) or on a return to author that ordering
       *  explicitly. The wet/dry balance is inherent in ``depth``, so the
       *  inherited ``impact()`` slider is not applied.
       *
       *  ### RT discipline
       *
       *  Steady-state ``process`` allocates nothing. The mixdown scratch is
       *  (re)sized only when the block length changes — the device-restart
       *  path, the one place the ``dspObject`` contract permits allocation.
       *  (The legacy implementation also carried the previous block's mixdown
       *  into the next one at 1/N gain — an accumulate-without-clear that
       *  diverges on mono buffers. This module zeroes the scratch every block
       *  instead.)
       */
      class API underWater : public dspObject {
      public:
        /** Defaults to ``depth() == 0`` — fully transparent. */
        underWater();
        virtual ~underWater() {}

        /** @brief The depth control input, in distance units below the water
         *  surface. Negative values clamp to 0 (above water). Callable from
         *  any control thread at control rate — allocation-free, wait-free
         *  (see class docs for the response curve). */
        underWater& depth(Flt value);

        /** @brief Current depth. */
        Flt depth() const;

        /** @brief dspObject lifecycle hook. Nothing to allocate up front: the
         *  mixdown scratch is sized in ``process`` (the block length is only
         *  known there). */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER& buffer);

      private:
        aFlt parmDepth; // control threads write, the audio thread reads

        // Audio-thread-owned state.
        buffer mono; // position-neutral mixdown scratch
        lowPass filter;
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // UNDERWATER_HPP_INCLUDED
