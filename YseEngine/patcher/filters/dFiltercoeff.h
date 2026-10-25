#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/filterCoefficients.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~filtercoeff``: biquad coefficients at signal rate, wrapping
     *         ``DSP::filterCoefficients`` (issue #1092).
     *
     *  Ported from Max's ``filtercoeff~``; its outlets feed ``~biquad``'s
     *  coefficient inlets. The wrapper only maps the pins, messages and
     *  parameter onto the DSP class.
     *
     *  - Inlet 0: the frequency in Hz, a number or a signal (read per
     *    sample); unconnected reads the last number sent to it (#1036).
     *  - Inlet 1: the linear gain, a number or a signal, likewise.
     *  - Inlet 2: the Q (the slope of a shelf), a number or a signal,
     *    likewise.
     *  - Any inlet takes a filter type word (``off``, ``lowpass``,
     *    ``highpass``, ``bandpass``, ``bandstop``, ``peaknotch``,
     *    ``lowshelf``, ``highshelf``, ``resonant``, ``allpass``,
     *    ``gainlpass``, ``gainhpass``, ``gainbpass``, ``gainbstop``,
     *    ``gainresonant``, ``gainapass``), optionally followed by the
     *    frequency and the Q, or for the types with a gain (``peaknotch``,
     *    the shelves and the ``gain`` types) the frequency, the gain and the
     *    Q; ``resamp <n>``; and a list of up to three numbers: the frequency,
     *    the gain and the Q. A command that arrives as a list (text through
     *    a receive) is read the same way.
     *  - Outlets 0-4: the coefficients a0, a1, a2, b1, b2.
     *  - Parameter: ``type``, the creation arguments: a filter type word and
     *    a resampling factor (``~filtercoeff peaknotch 4``). They are saved
     *    with the patch; a later message is not.
     *
     *  Everything reaches the audio thread through atomics, applied to the
     *  DSP class at the start of each block.
     */
    PATCHER_CLASS(dFiltercoeff, YSE::OBJ::D_FILTERCOEFF)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetFrequency)
    _INT_IN(SetFrequencyInt)
    _FLOAT_IN(SetGain)
    _INT_IN(SetGainInt)
    _FLOAT_IN(SetQ)
    _INT_IN(SetQInt)

  private:
    DSP::filterCoefficients fn;

    std::vector<std::string> arguments;

    std::atomic<int> type{DSP::filterCoefficients::LOWPASS};
    std::atomic<unsigned int> resampling{1};
    std::atomic<float> frequency{1000.f};
    std::atomic<float> gain{1.f};
    std::atomic<float> q{0.70710678f};
  };

} // namespace PATCHER
} // namespace YSE
