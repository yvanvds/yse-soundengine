/*
  ==============================================================================

    highpass.h
    Created: 3 Aug 2014 10:09:15pm
    Author:  yvan

  ==============================================================================
*/

#ifndef HIGHPASS_H_INCLUDED
#define HIGHPASS_H_INCLUDED

#include "../../../headers/defines.hpp"
#include "../../dspObject.hpp"
#include "../../filters.hpp"
#include "../../perChannel.hpp"

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /**
       *  @brief High-pass filter packaged as a chainable ``dspObject``.
       *
       *  Wraps ``DSP::highPass`` for use in a sound's DSP chain via
       *  ``YSE::sound::setDSP``. Processes every channel of the multichannel
       *  buffer independently, each with its own filter state (see the
       *  N-channel contract on ``dspObject::process``).
       */
      class API highPassFilter : public dspObject {
      public:
        highPassFilter();
        virtual ~highPassFilter() {};

        /** @brief Set the cutoff frequency in Hz. */
        highPassFilter& frequency(Flt value);

        /** @brief Current cutoff frequency. */
        Flt frequency();

        /** @brief dspObject lifecycle hook — allocates buffers. */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER& buffer);

      private:
        aFlt parmFrequency;
        DSP::buffer result; // per-block scratch, shared across channels
        perChannel<highPass> hp; // one filter per channel
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // HIGHPASS_H_INCLUDED
