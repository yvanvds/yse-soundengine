/*
  ==============================================================================

    morphingReverb.hpp
    Morphing reverb module — preset interpolation as a control input
    (issue #326).

  ==============================================================================
*/

#ifndef MORPHINGREVERB_HPP_INCLUDED
#define MORPHINGREVERB_HPP_INCLUDED

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/enums.hpp"
#include "../dspObject.hpp"
#include "../../internal/reverbDSP.h"
#include "../../reverb/reverbPresets.hpp"

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /**
       *  @brief A reverb whose preset interpolation is a *control input*.
       *
       *  This is the engine's zone/global reverb core (``INTERNAL::reverbDSP``)
       *  packaged as an ordinary chainable ``dspObject``, with the parameter
       *  blend generalized into a morph control (issue #326). Two endpoint
       *  parameter sets — slot A and slot B, each a named ``REVERB_PRESET`` or
       *  a custom ``REVERB::presetValues`` — are linearly interpolated by
       *  ``morph(t)``: 0 is pure A ("cathedral"), 1 is pure B ("closet"),
       *  anything in between is a genuine hybrid space.
       *
       *  ### Drivers
       *
       *  ``morph`` is deliberately driver-agnostic: a UI slider, a patcher
       *  ramp, a live-coded expression, MIDI — anything running on a control
       *  thread can call it at control rate. Writes are plain atomic stores
       *  (allocation-free, wait-free); the audio thread picks the value up on
       *  the next block and moves every underlying parameter through the
       *  reverb core's faders (~1000-sample ramps), so sweeps and jumps are
       *  click-free.
       *
       *  Listener-zone proximity — the classic game-audio driver — is *not*
       *  wired into this module: it remains the default driver of the same
       *  reverb core through the legacy ``REVERB::Manager`` path, computed
       *  exactly as before. Same core, different drivers.
       *
       *  ### Placement
       *
       *  The module is a plain ``dspObject``: attach it with
       *  ``YSE::channel::setDSP`` / ``YSE::sound::setDSP``, on any channel or
       *  on a return bus (``r.makeReturn("verb").setDSP(&morphVerb)``) — it is
       *  not welded to the global manager attach. The wet/dry balance rides
       *  the morphed presets themselves (each preset carries ``dry``/``wet``),
       *  so the inherited ``impact()`` is not applied; for send/return use,
       *  give both slots custom ``presetValues`` with ``dry = 0, wet = 1``.
       *  Unlike the legacy zone path (which applies historical makeup gain),
       *  the module maps ``dry = 1`` to unity — morphing fully to
       *  ``REVERB_OFF`` is a true bypass — and ``wet = 1`` to the reverb tank
       *  at its computed level.
       *
       *  ### Trajectory note (design record, issue #326)
       *
       *  With zone-bound return buses
       *  ([send_return_buses.md §12b](../../../docs/design/send_return_buses.md)),
       *  a reverb *zone* can instead be a return bus tied to a region of
       *  space, with proximity modulating the *send levels* into it — distinct
       *  spaces crossfading as real wet signals. The two tools coexist:
       *  **parameter morph** (this module) is one space transforming;
       *  **zone returns** are spaces crossfading. This module is kept an
       *  ordinary insert precisely so it can be dropped on such a return when
       *  zone returns land; the in-place ``REVERB::Manager`` path stays
       *  legacy-bound (bug fixes yes, new features no).
       *
       *  ### N-channel behaviour
       *
       *  Per-channel reverb state is sized to ``buffer.size()`` on the first
       *  ``process`` call and re-sized only when the channel count changes
       *  (the device-restart path — the only place the ``dspObject`` contract
       *  allows allocation). Steady-state ``process`` allocates nothing.
       */
      class API morphingReverb : public dspObject {
      public:
        /** Slots default to A = ``REVERB_GENERIC``, B = ``REVERB_HALL``,
         *  ``morph() == 0`` (pure A). */
        morphingReverb();
        virtual ~morphingReverb() {}

        /** @brief Set morph endpoint A from a named preset. */
        morphingReverb& presetA(REVERB_PRESET value);

        /** @brief Set morph endpoint A from a custom parameter set. Fields are
         *  stored individually-atomically; the faders smooth the (rare) block
         *  that observes a partly-updated slot. */
        morphingReverb& presetA(const REVERB::presetValues& value);

        /** @brief Current endpoint A parameter set. */
        REVERB::presetValues presetA() const;

        /** @brief Set morph endpoint B from a named preset. */
        morphingReverb& presetB(REVERB_PRESET value);

        /** @brief Set morph endpoint B from a custom parameter set. */
        morphingReverb& presetB(const REVERB::presetValues& value);

        /** @brief Current endpoint B parameter set. */
        REVERB::presetValues presetB() const;

        /** @brief The morph control input: 0 = pure A, 1 = pure B, clamped to
         *  [0, 1]. Callable from any control thread at control rate —
         *  allocation-free, click-free (see class docs). */
        morphingReverb& morph(Flt value);

        /** @brief Current morph position. */
        Flt morph() const;

        /** @brief dspObject lifecycle hook. Per-channel state is sized in
         *  ``process`` (the channel count is only known there). */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER& buffer);

      private:
        // One morph endpoint. Control threads write, the audio thread reads;
        // every field is individually atomic so writes are wait-free and
        // reads never tear a float. A whole-slot update is not atomic as a
        // set — the reverb faders smooth the one block that could observe a
        // mix of old and new fields.
        struct slot {
          aFlt roomsize, damp, dry, wet, modFrequency, modWidth;
          aFlt earlyTime[4];
          aFlt earlyGain[4];

          void store(const REVERB::presetValues& v);
          REVERB::presetValues load() const;
        };

        slot slotA;
        slot slotB;
        aFlt parmMorph; // [0, 1]

        INTERNAL::reverbDSP verb; // audio-thread-owned reverb core
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // MORPHINGREVERB_HPP_INCLUDED
