/*
  ==============================================================================

    sweep.hpp
    Created: 4 Sep 2015 10:49:34am
    Author:  yvan

  ==============================================================================
*/

#ifndef SWEEP_HPP_INCLUDED
#define SWEEP_HPP_INCLUDED

#include "../../../headers/defines.hpp"
#include "../../dspObject.hpp"
#include "../../filters.hpp"
#include "../../oscillators.hpp"
#include "../../interpolate4.hpp"
#include <memory>

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /**
       *  @brief Auto-wah / sweep filter — LFO-modulated resonant filter as a chainable
       * ``dspObject``.
       *
       *  Internally an oscillator drives a ``vcf`` to sweep its cutoff. The
       *  oscillator shape is selectable at construction. Choose ``SAW`` for a
       *  classic wah ramp, ``TRIANGLE`` for a smoother sweep, ``SQUARE`` for a
       *  step.
       */
      class API sweepFilter : public dspObject {
      public:
        /** @brief LFO shape that drives the filter sweep. */
        enum SHAPE {
          TRIANGLE,
          SAW,
          SQUARE,
        };

        /** @brief Construct with the chosen sweep shape. */
        sweepFilter(SHAPE shape = SAW);
        virtual ~sweepFilter() {};

        /** @brief Set the LFO speed in Hz. */
        sweepFilter& speed(Flt value);

        /** @brief Current LFO speed. */
        Flt speed();

        /** @brief Set the sweep depth as 0–100. */
        sweepFilter& depth(Int value);

        /** @brief Current sweep depth. */
        Int depth();

        /** @brief Set the centre frequency as 0–100. */
        sweepFilter& frequency(Int value);

        /** @brief Current centre frequency value. */
        Int frequency();

        /** @brief dspObject lifecycle hook — allocates buffers. */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER& buffer);

      private:
        aFlt parmSpeed;
        aInt parmDepth;
        aInt parmFrequency;

        SHAPE shape;
        std::shared_ptr<wavetable> table;
        std::shared_ptr<oscillator> osc;
        std::shared_ptr<vcf> filter;
        std::shared_ptr<DSP::buffer> result;
        std::shared_ptr<interpolate4> interpolator;
      };

    } // namespace MODULES

  } // namespace DSP

} // namespace YSE

#endif // SWEEP_HPP_INCLUDED
