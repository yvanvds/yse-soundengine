/*
  ==============================================================================

    delayTap.hpp
    One reader of a shared delayLine (issue #1105).

  ==============================================================================
*/

#ifndef YSE_DSP_DELAY_DELAYTAP_HPP
#define YSE_DSP_DELAY_DELAYTAP_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "delayLine.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Reads a ``delayLine`` at a delay time in milliseconds.
     *
     *  This is Max's ``tapout~``, one tap per object. Any number of taps can
     *  read the same line, each at its own delay.
     *
     *  - **A fixed delay** (the ``Flt`` overload) is rounded to whole samples
     *    and read without interpolation. A jump in the delay time is heard as
     *    a jump.
     *  - **A per-sample delay** (the ``buffer`` overload) is read with cubic
     *    (Catmull-Rom) interpolation, so a moving delay bends the pitch
     *    smoothly, as in a chorus or a flanger.
     *
     *  **Range.** The shortest delay is one block (``length`` samples), as in
     *  MSP: a tap never reads the block being written. With a per-sample
     *  delay it is two samples more, because the interpolator reads one
     *  sample ahead. The longest is the line's ``maximumDelay``. Delays
     *  outside the range are clamped to it. Because a tap only reads what
     *  was written before the line's ``beginBlock``, it gives the same output
     *  whether it runs before or after the line's ``write`` for that block.
     *
     *  The sample rate is read from ``SAMPLERATE`` on every block. Call it
     *  only from the audio thread. Processing allocates nothing: the output
     *  is sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API delayTap {
    public:
      delayTap();

      /** @brief One block at a fixed delay in milliseconds. */
      buffer& operator()(const delayLine& line, Flt delayMs, UInt length = STANDARD_BUFFERSIZE);

      /** @brief One block at a per-sample delay in milliseconds. */
      buffer& operator()(const delayLine& line, buffer& delayMs);

    private:
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_DELAY_DELAYTAP_HPP
