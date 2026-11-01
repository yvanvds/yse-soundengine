/*
  ==============================================================================

    schmittTrigger.hpp
    A threshold with hysteresis: a gate that opens at a high level and closes
    at a low one (issue #1122).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_SCHMITTTRIGGER_HPP
#define YSE_DSP_ANALYSIS_SCHMITTTRIGGER_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A Schmitt trigger: each output sample is 1 from the moment the
     *         input reaches the high threshold until it falls to the low
     *         one, and 0 otherwise.
     *
     *  Ported from Max's ``thresh~``. While the output is 0 it turns 1 on the
     *  first sample that is equal to or above the high (set) threshold. While
     *  it is 1 it turns 0 on the first sample that is equal to or below the
     *  low (reset) threshold. Between the two it holds, so a noisy signal
     *  that hovers around one level does not chatter: a robust gate.
     *
     *  When the two thresholds are equal there is no hysteresis: the output
     *  is 1 while the input is equal to or above the threshold and 0 while it
     *  is below it (``x >= t``), as in Max. A low threshold above the high
     *  one is used as the high one, which gives that same behaviour at the
     *  high threshold; the getters return the thresholds as set. Both default
     *  to 0.
     *
     *  The output starts at 0. The state at the end of a block carries into
     *  the next, so the result does not depend on how the signal is split
     *  into blocks. ``set()`` and ``reset()`` choose it, ``on()`` reads it.
     *
     *  An input sample that is not a number compares neither way, so the
     *  output holds. A threshold that is not a number is ignored; infinities
     *  are allowed (a high threshold of +infinity never opens the gate). The
     *  three-buffer call reads both thresholds per sample and sets the
     *  getters to the last ones read.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. There is
     *  no sample-rate dependent state. Call it from one thread at a time.
     *  Processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API schmittTrigger {
    public:
      schmittTrigger();

      /** @brief The low (reset) threshold: at or below it the output turns
       *         0. Not a number: ignored. */
      schmittTrigger& low(Flt threshold);

      /** @brief The high (set) threshold: at or above it the output turns
       *         1. Not a number: ignored. */
      schmittTrigger& high(Flt threshold);

      /** @brief The low threshold as last set or read. */
      Flt low() const {
        return lowThreshold;
      }

      /** @brief The high threshold as last set or read. */
      Flt high() const {
        return highThreshold;
      }

      /** @brief Sets the output state: the next sample holds it unless it
       *         crosses a threshold. */
      schmittTrigger& set(bool on);

      /** @brief Sets the output state to 0, as after construction. */
      schmittTrigger& reset();

      /** @brief The current output state: true while the output is 1. */
      bool on() const {
        return state;
      }

      /** @brief One sample: 1 or 0. */
      Flt process(Flt in);

      /** @brief One block at the current thresholds. */
      buffer& operator()(buffer& in);

      /** @brief One block with the low and high thresholds read per sample
       *         from @p lowSignal and @p highSignal; as long as the
       *         shortest. They also set the getters. */
      buffer& operator()(buffer& in, buffer& lowSignal, buffer& highSignal);

    private:
      Flt Step(Flt in);

      buffer output;

      Flt lowThreshold = 0.f;
      Flt highThreshold = 0.f;
      bool state = false;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_SCHMITTTRIGGER_HPP
