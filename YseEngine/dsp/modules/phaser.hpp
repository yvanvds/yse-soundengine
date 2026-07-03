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
       *  the familiar sweeping "jet plane" sound.
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

      private:
        aFlt parmFrequency;
        aFlt parmRange;

        std::shared_ptr<lfo> triangle;
        std::shared_ptr<realOneZeroReversed> rzero1;
        std::shared_ptr<realOnePole> rpole1;
        std::shared_ptr<realOneZeroReversed> rzero2;
        std::shared_ptr<realOnePole> rpole2;
        std::shared_ptr<realOneZeroReversed> rzero3;
        std::shared_ptr<realOnePole> rpole3;
        std::shared_ptr<realOneZeroReversed> rzero4;
        std::shared_ptr<realOnePole> rpole4;

        std::shared_ptr<buffer> result;
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // PHASER_HPP_INCLUDED
