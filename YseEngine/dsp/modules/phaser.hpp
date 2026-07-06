/*
  ==============================================================================

    phaser.hpp
    Created: 15 Sep 2015 7:54:15pm
    Author:  yvan

  ==============================================================================
*/

#ifndef PHASER_HPP_INCLUDED
#define PHASER_HPP_INCLUDED

#include "../dspObject.hpp"
#include "../lfo.hpp"
#include "../rawFilters.hpp"
#include "../perChannel.hpp"
#include <memory>

#include "../math.hpp"
#include "../filters.hpp"

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /**
       *  @brief Phaser effect as a chainable ``dspObject``.
       *
       *  Four-stage all-pass cascade modulated by a triangle LFO. Produces
       *  the familiar sweeping "jet plane" sound. Processes every channel of
       *  the multichannel buffer independently — the LFO is shared (one sweep
       *  for the whole buffer) but each channel keeps its own all-pass cascade
       *  state (see the N-channel contract on ``dspObject::process``).
       */
      class API phaser : public dspObject {
      public:
        phaser();
        virtual ~phaser() {};

        /** @brief Set the sweep LFO frequency. Typically very low; default 0.3 Hz. Must be > 0. */
        phaser& frequency(Flt value);

        /** @brief Current LFO frequency. */
        Flt frequency();

        /** @brief Set the sweep range coefficient. Default 0.1; values above 0.5 are unstable. */
        phaser& range(Flt value);

        /** @brief Current sweep range. */
        Flt range();

        /** @brief dspObject lifecycle hook — allocates buffers. */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER& buffer);

        /** @brief One channel's four-stage all-pass cascade state. */
        struct allpassChain {
          realOneZeroReversed rzero1, rzero2, rzero3, rzero4;
          realOnePole rpole1, rpole2, rpole3, rpole4;
        };

      private:
        aFlt parmFrequency;
        aFlt parmRange;

        std::shared_ptr<lfo> triangle; // shared LFO — one sweep for the buffer
        DSP::buffer result; // per-block scratch, shared across channels
        perChannel<allpassChain> chain; // one cascade per channel
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // PHASER_HPP_INCLUDED
