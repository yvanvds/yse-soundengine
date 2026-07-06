/*
  ==============================================================================

    feedbackDelay.hpp
    Multichannel feedback delay module (issue #160).

  ==============================================================================
*/

#ifndef FEEDBACKDELAY_HPP_INCLUDED
#define FEEDBACKDELAY_HPP_INCLUDED

#include "../../../headers/defines.hpp"
#include "../../dspObject.hpp"
#include "../../delay.hpp"
#include "../../filters.hpp"
#include "../../perChannel.hpp"
#include <cstddef>

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /**
       *  @brief Mix-grade feedback delay packaged as a chainable ``dspObject``.
       *
       *  Where ``basicDelay`` is a three-tap feed-forward building block, this
       *  is a recirculating delay meant to sit on a channel insert or a send
       *  return: a per-channel delay line with a feedback path, a damping
       *  low-pass filter in that path (successive echoes darken, the classic
       *  tape character), and cross-feed between channel pairs for ping-pong.
       *  The wet/dry balance is the inherited ``impact()`` — ``impact(1)`` is
       *  echoes only (send use), a lower value keeps the dry signal (insert
       *  use).
       *
       *  Processes every channel of the multichannel buffer independently, each
       *  with its own delay line and damping filter (see the N-channel contract
       *  on ``dspObject::process``). Cross-feed pairs adjacent channels
       *  (0<->1, 2<->3, ...); an unpaired trailing channel feeds back only
       *  into itself.
       *
       *  Delay-time changes are smoothed per-sample so retuning the delay does
       *  not click. All buffers are sized on the ``create()`` / channel-count
       *  change path; ``process`` allocates nothing in steady state.
       */
      class API feedbackDelay : public dspObject {
      public:
        feedbackDelay();
        virtual ~feedbackDelay() {};

        /** @brief Set the delay time in milliseconds (clamped to the module's
         *  maximum). Changes are ramped to avoid zipper noise. */
        feedbackDelay& time(Flt ms);

        /** @brief Current delay time in milliseconds. */
        Flt time();

        /** @brief Set the feedback amount in [0, 0.99]. Higher sustains echoes
         *  longer; the upper bound keeps the loop stable. */
        feedbackDelay& feedback(Flt amount);

        /** @brief Current feedback amount. */
        Flt feedback();

        /** @brief Set the damping cut-off in Hz — a low-pass in the feedback
         *  path. Lower values darken successive echoes faster. */
        feedbackDelay& damping(Flt hz);

        /** @brief Current damping cut-off in Hz. */
        Flt damping();

        /** @brief Set the cross-feed amount in [0, 1] between channel pairs.
         *  0 keeps channels independent; 1 is full ping-pong (each channel's
         *  feedback comes entirely from its partner). */
        feedbackDelay& crossfeed(Flt amount);

        /** @brief Current cross-feed amount. */
        Flt crossfeed();

        /** @brief dspObject lifecycle hook. */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER& buffer);

      private:
        aFlt parmTime; // delay time, milliseconds
        aFlt parmFeedback; // feedback amount, [0, 0.99]
        aFlt parmDamping; // damping low-pass cut-off, Hz
        aFlt parmCrossfeed; // cross-feed amount, [0, 1]

        // Audio-thread-only smoothing state for the delay time.
        Flt currentTime; // last smoothed delay time (ms)
        Flt timeSmoothCoef; // one-pole coefficient, computed in create()
        Bool primed; // whether currentTime has been seeded yet
        std::size_t blockLength; // last seen block length (scratch sizing)

        /** @brief One channel's recirculating delay state. */
        struct delayChannel {
          DSP::delay line; // the delay line
          DSP::lowPass damper; // damping filter in the feedback path
          DSP::buffer damped; // this block's damped feedback signal (owned)
          DSP::buffer fbSaved; // feedback to inject on the next block
          delayChannel();
        };

        perChannel<delayChannel> channels;

        // Per-block scratch, shared across channels (sized on length change).
        DSP::buffer toWrite; // dry input + recirculated feedback
        DSP::buffer delayed; // raw delay-line output (wet tap)
        DSP::buffer timeBuffer; // per-sample smoothed delay time (ms)
        DSP::buffer crossScratch; // partner term while mixing cross-feed
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // FEEDBACKDELAY_HPP_INCLUDED
