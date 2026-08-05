/*
  ==============================================================================

    ringModulator.h
    Created: 31 Jan 2014 2:56:31pm
    Author:  yvan

  ==============================================================================
*/

#ifndef RINGMODULATOR_H_INCLUDED
#define RINGMODULATOR_H_INCLUDED

#include "../../headers/defines.hpp"
#include "../dspObject.hpp"
#include "../oscillators.hpp"
#include <memory>

namespace YSE {
  namespace DSP {

    /**
     *  @brief Ring modulator as a chainable ``dspObject``.
     *
     *  Multiplies the input by an internal sine carrier — classic
     *  bell-like / robot-voice timbres. The carrier frequency is the only
     *  user-controllable parameter.
     */
    class API ringModulator : public dspObject {
    public:
      ringModulator();
      ~ringModulator() override {}

      /** @brief Set the carrier frequency in Hz. */
      ringModulator& frequency(Flt value);

      /** @brief Current carrier frequency. */
      Flt frequency();

      /** @brief dspObject lifecycle hook — allocates buffers. */
      void create() override;

      /** @brief dspObject audio-thread entry point. */
      void process(MULTICHANNELBUFFER& buffer) override;

    private:
      aFlt parmFrequency;
      aFlt parmLevel;
      std::shared_ptr<sine> sineGen;
      std::shared_ptr<buffer> result;
    };

  } // namespace DSP
} // namespace YSE

#endif // RINGMODULATOR_H_INCLUDED
