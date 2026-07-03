/*
  ==============================================================================

    lowpass.h
    Created: 3 Aug 2014 10:08:59pm
    Author:  yvan

  ==============================================================================
*/

#ifndef LOWPASS_H_INCLUDED
#define LOWPASS_H_INCLUDED

#include "../../../headers/defines.hpp"
#include "../../dspObject.hpp"
#include "../../filters.hpp"
#include <memory>

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /**
       *  @brief Low-pass filter packaged as a chainable ``dspObject``.
       *
       *  Wraps ``DSP::lowPass`` for use in a sound's DSP chain via
       *  ``YSE::sound::setDSP``.
       *
       *  @note Mono only — feeds the first channel of the multichannel buffer.
       */
      class API lowPassFilter : public dspObject {
      public:
        lowPassFilter();
        virtual ~lowPassFilter() {};

        /** @brief Set the cutoff frequency in Hz. */
        lowPassFilter& frequency(Flt value);

        /** @brief Current cutoff frequency. */
        Flt frequency();

        /** @brief dspObject lifecycle hook — allocates buffers. */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER& buffer);

      private:
        aFlt parmFrequency;
        std::shared_ptr<DSP::buffer> result;
        std::shared_ptr<lowPass> lp;
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // LOWPASS_H_INCLUDED
