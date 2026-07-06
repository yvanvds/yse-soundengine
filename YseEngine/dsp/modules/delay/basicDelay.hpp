/*
  ==============================================================================

    basicDelay.hpp
    Created: 30 Sep 2015 4:40:17pm
    Author:  yvan

  ==============================================================================
*/

#ifndef BASICDELAY_HPP_INCLUDED
#define BASICDELAY_HPP_INCLUDED

#include "../../../headers/defines.hpp"
#include "../../dspObject.hpp"
#include "../../delay.hpp"
#include "../../perChannel.hpp"
#include <cstddef>

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /**
       *  @brief Three-tap delay packaged as a chainable ``dspObject``.
       *
       *  Reads the same delay line at up to three offsets, each with its own
       *  time and gain. Use ``highPassDelay`` / ``lowPassDelay`` for the same
       *  effect with a filter in the feedback path.
       *
       *  Processes every channel of the multichannel buffer independently, each
       *  with its own delay line (see the N-channel contract on
       *  ``dspObject::process``).
       */
      class API basicDelay : public dspObject {
      public:
        /** @brief Which of the three delay taps to address. */
        enum DELAY_NR {
          FIRST,
          SECOND,
          THIRD,
        };

        basicDelay();
        virtual ~basicDelay() {};

        /** @brief Configure one of the three taps.
         *  @param nr   Tap to configure.
         *  @param time Delay time in milliseconds.
         *  @param gain Gain for this tap.
         */
        basicDelay& set(DELAY_NR nr, Flt time, Flt gain);

        /** @brief Current delay time of tap ``nr``. */
        Flt time(DELAY_NR nr);

        /** @brief Current gain of tap ``nr``. */
        Flt gain(DELAY_NR nr);

        /** @brief dspObject lifecycle hook — allocates buffers. */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER& buffer);

      protected:
        /** @brief Hook for subclasses to size their per-channel pre-filter state
         *  to ``count`` channels. Called from ``process`` on the (rare)
         *  channel-count-change path. Base implementation does nothing. */
        virtual void ensurePreFilter(std::size_t count);

        /** @brief Hook for subclasses to apply their pre-filter for channel
         *  ``ch`` to ``buffer`` before it enters the delay line. Base
         *  implementation does nothing. */
        virtual void applyPreFilter(DSP::buffer& buffer, std::size_t ch);

        aFlt time0, time1, time2;
        aFlt gain0, gain1, gain2;

        /** @brief One channel's delay line. */
        struct delayChannel {
          DSP::delay line;
          delayChannel();
        };

        DSP::buffer result; // per-block scratch, shared across channels
        DSP::buffer reader; // per-block scratch, shared across channels
        perChannel<delayChannel> channels;
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // BASICDELAY_HPP_INCLUDED
