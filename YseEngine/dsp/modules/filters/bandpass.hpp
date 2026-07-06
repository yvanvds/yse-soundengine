/*
  ==============================================================================

    bandpass.h
    Created: 3 Aug 2014 10:09:23pm
    Author:  yvan

  ==============================================================================
*/

#ifndef BANDPASS_H_INCLUDED
#define BANDPASS_H_INCLUDED

#include "../../../headers/defines.hpp"
#include "../../dspObject.hpp"
#include "../../filters.hpp"
#include "../../perChannel.hpp"

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /**
       *  @brief Resonant band-pass filter packaged as a chainable ``dspObject``.
       *
       *  Wraps ``DSP::bandPass`` for use in a sound's DSP chain via
       *  ``YSE::sound::setDSP``. Processes every channel of the multichannel
       *  buffer independently, each with its own filter state (see the
       *  N-channel contract on ``dspObject::process``).
       */
      class API bandPassFilter : public dspObject {
      public:
        bandPassFilter();
        virtual ~bandPassFilter() {};

        /** @brief Set the centre frequency in Hz. */
        bandPassFilter& frequency(Flt value);

        /** @brief Current centre frequency. */
        Flt frequency();

        /** @brief Set the resonance (Q factor). */
        bandPassFilter& setQ(Flt value);

        /** @brief Current Q factor. */
        Flt getQ();

        /** @brief dspObject lifecycle hook — allocates buffers. */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER& buffer);

      private:
        aFlt parmFrequency;
        aFlt parmQ;
        DSP::buffer result; // per-block scratch, shared across channels
        perChannel<bandPass> bp; // one filter per channel
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // BANDPASS_H_INCLUDED
